/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <algorithm>
#include <condition_variable>
#include <limits>
#include <turbo/cardano.hpp>
#include <turbo/cardano/network/common.hpp>
#include <turbo/chunk-registry.hpp>
#include <turbo/sync/p2p.hpp>

namespace turbo::sync::p2p {
    using namespace turbo::cardano::network;
    using namespace turbo::cardano;

    struct inflight_budget: std::enable_shared_from_this<inflight_budget> {
        struct lease {
            lease(std::shared_ptr<inflight_budget> budget, const uint64_t amount)
                : _budget { std::move(budget) }, _amount { amount }
            {
            }

            ~lease()
            {
                _budget->_release(_amount);
            }

            lease(const lease &) =delete;
            lease &operator=(const lease &) =delete;
        private:
            std::shared_ptr<inflight_budget> _budget;
            uint64_t _amount;
        };

        explicit inflight_budget(const uint64_t limit): _limit { limit }
        {
            if (!_limit) [[unlikely]]
                throw error("the sync in-flight memory budget must be greater than zero");
        }

        std::shared_ptr<lease> acquire(const uint64_t amount)
        {
            std::unique_lock lk { _mutex };
            _cv.wait(lk, [&] {
                // Always admit one oversized item when the budget is otherwise empty.
                return _used == 0 || (amount <= _limit && _used <= _limit - amount);
            });
            _used += amount;
            return std::make_shared<lease>(shared_from_this(), amount);
        }
    private:
        std::mutex _mutex {};
        std::condition_variable _cv {};
        const uint64_t _limit;
        uint64_t _used = 0;

        void _release(const uint64_t amount)
        {
            {
                std::scoped_lock lk { _mutex };
                _used -= amount;
            }
            _cv.notify_all();
        }
    };

    struct syncer::impl {
        impl(syncer &parent, client_manager &cm, const size_t max_inflight_bytes)
            : _parent { parent }, _client_manager { cm }, _raw_dir { _parent.local_chain().data_dir() / "raw" },
                _inflight_budget { std::make_shared<inflight_budget>(max_inflight_bytes) }
        {
            std::filesystem::create_directories(_raw_dir);
            logger::info("sync in-flight memory budget: {} MiB", max_inflight_bytes >> 20);
        }

        // Finds a peer and the best intersection point
        [[nodiscard]] std::shared_ptr<sync::peer_info> find_peer(std::optional<network::address> addr, const version_config_t &versions) const
        {
            logger::info("connecting to peer {} requesting versions [{};{}]", addr, versions.min, versions.max);
            if (!addr)
                addr = _parent.peer_list().next_cardano();
            auto client = _client_manager.connect(*addr, versions);
            // Try to fit into a single packet of 1460 bytes : 37 * (33 + 5) + message header + segment header (8 bytes)
            static constexpr ptrdiff_t points_per_query = 37;
            auto first_it = _parent.local_chain().cbegin();
            auto last_it = _parent.local_chain().cend();
            std::optional<point3> tip {};
            while (last_it - first_it > 1) {
                const auto distance = last_it - first_it;
                const auto step = std::max(ptrdiff_t { 1 }, distance / points_per_query);

                point2_list points {};
                for (auto it = first_it; it != last_it; it = it + std::min(step, last_it - it)) {
                    points.emplace_back(it->slot, it->hash);
                }
                std::ranges::reverse(points);
                const auto intersection = client->find_intersection_sync(points);
                if (!intersection.isect) [[unlikely]]
                    throw error("internal error: wasn't able to narrow down the intersection point to a block!");

                const auto isect_it = _parent.local_chain().find_block(*intersection.isect);
                if (isect_it == _parent.local_chain().cend()) [[unlikely]]
                    throw error(fmt::format("failed to find a local block {}:{}", intersection.isect->slot, intersection.isect->hash));

                if (const auto next_last_it = isect_it + std::min(ptrdiff_t { step }, (last_it - isect_it)); next_last_it != _parent.local_chain().cend())
                    last_it = next_last_it;
                first_it = isect_it;
                tip = intersection.tip;
            }
            if (!tip)
                tip = client->find_tip_sync();
            if (first_it == _parent.local_chain().cend()) [[unlikely]]
                return std::make_shared<peer_info>(std::move(client), point::from_point3(*tip));
            return std::make_shared<peer_info>(std::move(client), point::from_point3(*tip), first_it->point());
        }

        void sync_attempt(peer_info &peer, const cardano::optional_slot max_slot)
        {
            // target offset is unbounded since the cardano network protocol does not provide the size information
            _invalid_first_offset.store(no_recorded_value, std::memory_order_relaxed);
            _next_chunk_offset = 0;
            _last_chunk.clear();
            _last_chunk_id.reset();
            _last_block_slot.store(no_recorded_value, std::memory_order_relaxed);

            if (peer.intersection())
                _next_chunk_offset = _parent.local_chain().get_block_info(*peer.intersection()).end_offset();
            _sync(peer, peer.intersection(), max_slot);
            _add_last_chunk_if_not_empty();
            _parent.local_chain().sched().process();
        }

        void cancel_tasks(const uint64_t max_valid_offset)
        {
            logger::debug("sync::p2p::cancel_tasks max_valid_offset: {}", max_valid_offset);
            mutex::scoped_lock lk { _invalid_mutex };
            const auto last_val = _invalid_first_offset.load(std::memory_order_relaxed);
            if (last_val == no_recorded_value || last_val > max_valid_offset) {
                _invalid_first_offset.store(max_valid_offset, std::memory_order_relaxed);
                const auto num_tasks = _parent.local_chain().sched().cancel([max_valid_offset](const auto &, const auto &param) {
                    return param && param->type() == typeid(chunk_offset_t) && std::any_cast<chunk_offset_t>(*param) >= max_valid_offset;
                });
                logger::warn("validation failure at offset {}: cancelled {} validation tasks", max_valid_offset, num_tasks);
            }
        }
    private:
        static constexpr size_t max_retries = 3;
        static constexpr uint64_t no_recorded_value = std::numeric_limits<uint64_t>::max();

        struct ready_chunk {
            std::string path {};
            std::string name {};
            uint64_t size {};
            block_hash hash {};
        };

        syncer &_parent;
        client_manager &_client_manager;
        std::filesystem::path _raw_dir;
        std::shared_ptr<inflight_budget> _inflight_budget;

        uint8_vector _last_chunk{};
        std::optional<uint64_t> _last_chunk_id {};
        std::atomic<uint64_t> _last_block_slot { no_recorded_value };
        uint64_t _next_chunk_offset = 0;
        uint64_t _parse_start_chunk_id = 0;
        uint64_t _parse_target_chunk_id = 0;
        uint64_t _parse_next_chunk_id = 0;

        alignas(mutex::alignment) mutex::unique_lock::mutex_type _invalid_mutex{};
        std::atomic<uint64_t> _invalid_first_offset { no_recorded_value };

        static void _record_monotonic(std::atomic<uint64_t> &target, const uint64_t value, auto should_replace)
        {
            for (;;) {
                auto current = target.load(std::memory_order_relaxed);
                if (current != no_recorded_value && !should_replace(value, current))
                    break;
                if (target.compare_exchange_weak(current, value, std::memory_order_relaxed, std::memory_order_relaxed))
                    break;
            }
        }

        void _record_invalid_offset(const uint64_t chunk_offset)
        {
            _record_monotonic(_invalid_first_offset, chunk_offset, [](const auto candidate, const auto current) {
                return candidate < current;
            });
        }

        void _record_last_block_slot(const uint64_t slot)
        {
            _record_monotonic(_last_block_slot, slot, [](const auto candidate, const auto current) {
                return candidate > current;
            });
        }

        void _report_chunk_download_progress(const progress_point &progress)
        {
            _record_last_block_slot(progress.slot);
            _parent.local_chain().report_progress("download", progress);
        }

        void _sync(peer_info &peer, const std::optional<point> &local_tip, const std::optional<uint64_t> &max_slot)
        {
            std::optional<point> continue_from = local_tip;
            const std::optional<uint64_t> target_chunk_id = max_slot.transform([&](const auto slot) {
                return cardano::slot{slot, _parent.local_chain().config()}.chunk_id();
            });
            const auto [headers, tip] = peer.client().fetch_headers_sync(continue_from, 1, true);
            if (!headers.empty() && (!max_slot || headers.front().slot <= *max_slot)) {
                _parse_start_chunk_id = cardano::slot { headers.front().slot, _parent.local_chain().config() }.chunk_id();
                _parse_target_chunk_id = cardano::slot { max_slot.value_or(tip.slot), _parent.local_chain().config() }.chunk_id();
                _parse_next_chunk_id = _parse_start_chunk_id;
                // current implementation of fetch_blocks does not leave its connection in a working state
                std::optional<std::string> err{};
                try {
                    peer.client().fetch_blocks(headers.front(), tip, [&](auto resp) {
                        return std::visit([&](auto &&rv) -> bool {
                            using T = std::decay_t<decltype(rv)>;
                            if constexpr (std::is_same_v<T, client::error_msg>) {
                                err = std::move(rv);
                                return false;
                            } else if constexpr (std::is_same_v<T, client::msg_block_t>) {
                                auto blk = std::make_unique<parsed_block>(rv.bytes);
                                if (_invalid_first_offset.load(std::memory_order_relaxed) != no_recorded_value
                                        || (max_slot && blk->blk->slot() > *max_slot))
                                    return false;
                                _add_block(blk->blk);
                                return true;
                            } else if constexpr (std::is_same_v<T, client::msg_compressed_blocks_t>) {
                                if (rv.encoding != T::encoding_zstd_fast && rv.encoding != T::encoding_zstd_max) [[unlikely]] {
                                    logger::error("unsupported encoding: {}", rv.encoding);
                                    return false;
                                }
                                if (_invalid_first_offset.load(std::memory_order_relaxed) != no_recorded_value)
                                    return false;
                                // The compressed protocol emits one message per storage chunk, in slot order.
                                const auto compression_level = rv.compression_level();
                                _add_compressed_chunk(std::move(rv.payload), compression_level,
                                    _parse_priority(_parse_next_chunk_id++));
                                if (target_chunk_id) {
                                    const auto last_slot = _last_block_slot.load(std::memory_order_relaxed);
                                    if (last_slot != no_recorded_value
                                            && cardano::slot { last_slot, _parent.local_chain().config() }.chunk_id() >= *target_chunk_id)
                                        return false;
                                }
                                return true;
                            } else {
                                logger::error("unsupported message: {}", typeid(T).name());
                                return false;
                            }
                        }, std::move(resp));
                    });
                } catch (const std::exception &ex) {
                    if (!err)
                        err = ex.what();
                }
                _add_last_chunk_if_not_empty();
                peer.client().process(&_parent.local_chain().sched());
                _parent.local_chain().sched().process();
                if (err) [[unlikely]]
                    throw error(fmt::format("fetch_block has failed with error: {}", err));
            }
        }

        static uint64_t _inflight_cost(const uint64_t uncompressed_size, const uint64_t compressed_capacity)
        {
            // The fixed part leaves room for block metadata, chunk indexers, and other per-task allocations.
            static constexpr uint64_t fixed_parse_overhead = uint64_t { 32 } << 20;
            return fixed_parse_overhead + uncompressed_size + compressed_capacity;
        }

        [[nodiscard]] int64_t _parse_priority(const uint64_t chunk_id) const
        {
            static constexpr int64_t priority_min = 100;
            static constexpr int64_t priority_spread = 99;
            if (_parse_target_chunk_id <= _parse_start_chunk_id)
                return priority_min + priority_spread;
            const auto current_chunk_id = std::clamp(chunk_id, _parse_start_chunk_id, _parse_target_chunk_id);
            const auto span = _parse_target_chunk_id - _parse_start_chunk_id;
            const auto remaining = _parse_target_chunk_id - current_chunk_id;
            // Earlier chunks are more likely to unblock ledger advancement.
            return priority_min + static_cast<int64_t>(static_cast<long double>(priority_spread) * remaining / span);
        }

        void _add_compressed_chunk(uint8_vector compressed, const int32_t compression_level, const int64_t priority)
        {
            const auto uncompressed_size = zstd::decompressed_size(compressed);
            if (uncompressed_size > zstd::max_zstd_buffer) [[unlikely]]
                throw error(fmt::format("compressed chunk expands to {} bytes, exceeding the maximum of {}",
                    uncompressed_size, zstd::max_zstd_buffer));
            const auto chunk_offset = _next_chunk_offset;
            _next_chunk_offset += uncompressed_size;
            auto budget_lease = _inflight_budget->acquire(_inflight_cost(uncompressed_size, compressed.capacity()));
            auto compressed_ptr = std::make_shared<uint8_vector>(std::move(compressed));
            _parent.local_chain().sched().submit("parse", priority,
                [this, compressed=std::move(compressed_ptr), chunk_offset=chunk_offset,
                    compression_level=compression_level, budget_lease=std::move(budget_lease)]() mutable {
                    static_cast<void>(budget_lease);
                    const auto ex_ptr = logger::run_log_errors([&] {
                        auto uncompressed = zstd::decompress(*compressed);
                        auto progress = _parent.local_chain().add_buffer(
                            chunk_offset, std::move(uncompressed), std::move(*compressed), compression_level);
                        _report_chunk_download_progress(progress);
                    });
                    if (ex_ptr) [[unlikely]]
                        _record_invalid_offset(chunk_offset);
                });
        }

        void _add_chunk(uint8_vector uncompressed, const int64_t priority)
        {
            if (uncompressed.size() > zstd::max_zstd_buffer) [[unlikely]]
                throw error(fmt::format("chunk has {} bytes, exceeding the maximum of {}",
                    uncompressed.size(), zstd::max_zstd_buffer));
            const auto chunk_offset = _next_chunk_offset;
            _next_chunk_offset += uncompressed.size();
            const auto compressed_capacity = ZSTD_compressBound(uncompressed.size());
            auto budget_lease = _inflight_budget->acquire(_inflight_cost(uncompressed.capacity(), compressed_capacity));
            auto uncompressed_ptr = std::make_shared<uint8_vector>(std::move(uncompressed));
            _parent.local_chain().sched().submit("parse", priority,
                [this, uncompressed=std::move(uncompressed_ptr), chunk_offset=chunk_offset,
                    budget_lease=std::move(budget_lease)]() mutable {
                    static_cast<void>(budget_lease);
                    const auto ex_ptr = logger::run_log_errors([&] {
                        auto compressed = zstd::compress(*uncompressed, 21);
                        auto progress = _parent.local_chain().add_buffer(
                            chunk_offset, std::move(*uncompressed), std::move(compressed), 21);
                        _report_chunk_download_progress(progress);
                    });
                    if (ex_ptr) [[unlikely]]
                        _record_invalid_offset(chunk_offset);
                });
        }

        void _add_last_chunk_if_not_empty()
        {
            if (!_last_chunk.empty()) {
                if (!_last_chunk_id) [[unlikely]]
                    throw error("a non-empty sync chunk has no chunk id");
                _add_chunk(std::move(_last_chunk), _parse_priority(*_last_chunk_id));
                _last_chunk.clear();
                _last_chunk_id.reset();
            }
        }

        void _add_block(const block_container &blk)
        {
            const auto blk_slot = _parent.local_chain().make_slot(blk->slot());
            const auto last_block_slot = _last_block_slot.load(std::memory_order_relaxed);
            if (last_block_slot != no_recorded_value && last_block_slot > blk_slot) [[unlikely]]
                throw error(fmt::format("unexpected block order: block with slot {} comes after slot {}", blk_slot, cardano::slot { last_block_slot, _parent.local_chain().config() }));
            _parent.local_chain().report_progress("download", { blk_slot, blk.end_offset() });
            if (last_block_slot == no_recorded_value
                    || cardano::slot { last_block_slot, _parent.local_chain().config() }.chunk_id() != blk_slot.chunk_id()) {
                logger::info("block from a new chunk: slot: {} hash: {} height: {}", blk_slot, blk->hash(), blk->height());
                _add_last_chunk_if_not_empty();
                _last_chunk_id = blk_slot.chunk_id();
            }
            _record_last_block_slot(blk_slot);
            _last_chunk << blk.raw();
        }
    };

    syncer::syncer(chunk_registry &cr, const size_t max_inflight_bytes)
        : syncer { cr, peer_selection_simple::get(), client_manager_async::get(), max_inflight_bytes }
    {
    }

    syncer::syncer(chunk_registry &cr, peer_selection &ps, client_manager &ccm, const size_t max_inflight_bytes)
        : sync::syncer { cr, ps }, _impl { std::make_unique<impl>(*this, ccm, [&] {
            if (max_inflight_bytes != auto_max_inflight_bytes)
                return max_inflight_bytes;
            const auto num_workers = cr.sched().num_workers();
            if (num_workers > std::numeric_limits<size_t>::max() / inflight_bytes_per_worker) [[unlikely]]
                throw error(fmt::format("scheduler worker count {} is too large", num_workers));
            return num_workers * inflight_bytes_per_worker;
        }()) }
    {
    }

    syncer::~syncer() =default;

    [[nodiscard]] std::shared_ptr<sync::peer_info> syncer::find_peer(std::optional<network::address> addr, const version_config_t &versions) const
    {
        return _impl->find_peer(addr, versions);
    }

    void syncer::cancel_tasks(const uint64_t max_valid_offset)
    {
        _impl->cancel_tasks(max_valid_offset);
    }

    void syncer::sync_attempt(sync::peer_info &peer, const cardano::optional_slot max_slot)
    {
        _impl->sync_attempt(dynamic_cast<peer_info &>(peer), max_slot);
    }
}
