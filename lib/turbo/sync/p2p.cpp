/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <algorithm>
#include <turbo/cardano.hpp>
#include <turbo/cardano/network/common.hpp>
#include <turbo/chunk-registry.hpp>
#include <turbo/sync/p2p.hpp>

namespace turbo::sync::p2p {
    using namespace turbo::cardano::network;
    using namespace turbo::cardano;

    struct syncer::impl {
        impl(syncer &parent, client_manager &cm)
            : _parent { parent }, _client_manager { cm }, _raw_dir { _parent.local_chain().data_dir() / "raw" }
        {
            std::filesystem::create_directories(_raw_dir);
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
                if (!intersection.isect)
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
            _invalid_first_offset.store(std::optional<uint64_t>{}, std::memory_order_relaxed);
            _next_chunk_offset = 0;
            _last_chunk.clear();
            _last_block_slot.store(std::optional<uint64_t> {}, std::memory_order_relaxed);

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
            if (!last_val || *last_val > max_valid_offset) {
                _invalid_first_offset.store(max_valid_offset, std::memory_order_relaxed);
                const auto num_tasks = _parent.local_chain().sched().cancel([max_valid_offset](const auto &, const auto &param) {
                    return param && param->type() == typeid(chunk_offset_t) && std::any_cast<chunk_offset_t>(*param) >= max_valid_offset;
                });
                logger::warn("validation failure at offset {}: cancelled {} validation tasks", max_valid_offset, num_tasks);
            }
        }
    private:
        static constexpr size_t max_retries = 3;

        struct ready_chunk {
            std::string path {};
            std::string name {};
            uint64_t size {};
            block_hash hash {};
        };

        syncer &_parent;
        client_manager &_client_manager;
        std::filesystem::path _raw_dir;

        uint8_vector _last_chunk{};
        std::atomic<std::optional<uint64_t>> _last_block_slot{};
        uint64_t _next_chunk_offset = 0;

        alignas(mutex::alignment) mutex::unique_lock::mutex_type _invalid_mutex{};
        std::atomic<std::optional<uint64_t>> _invalid_first_offset{};

        static void _record_optional_monotonic(std::atomic<std::optional<uint64_t>> &target, const uint64_t value, auto should_replace)
        {
            const std::optional<uint64_t> next { value };
            for (;;) {
                auto current = target.load(std::memory_order_relaxed);
                if (current && !should_replace(value, *current))
                    break;
                if (target.compare_exchange_weak(current, next, std::memory_order_relaxed, std::memory_order_relaxed))
                    break;
            }
        }

        void _record_invalid_offset(const uint64_t chunk_offset)
        {
            _record_optional_monotonic(_invalid_first_offset, chunk_offset, [](const auto candidate, const auto current) {
                return candidate < current;
            });
        }

        void _record_last_block_slot(const uint64_t slot)
        {
            _record_optional_monotonic(_last_block_slot, slot, [](const auto candidate, const auto current) {
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
                                if (_invalid_first_offset.load(std::memory_order_relaxed) || (max_slot && blk->blk->slot() > *max_slot))
                                    return false;
                                _add_block(blk->blk);
                                return true;
                            } else if constexpr (std::is_same_v<T, client::msg_compressed_blocks_t>) {
                                if (rv.encoding != 1) [[unlikely]] {
                                    logger::error("unsupported encoding: {}", rv.encoding);
                                    return false;
                                }
                                if (_invalid_first_offset.load(std::memory_order_relaxed))
                                    return false;
                                // the comma operator has no ordering guarantees so must decompress before the data is moved
                                auto uncompressed = rv.bytes();
                                _add_chunk(std::move(uncompressed), std::move(rv.payload));
                                if (target_chunk_id) {
                                    const auto last_slot = _last_block_slot.load(std::memory_order_relaxed);
                                    if (last_slot && cardano::slot { *last_slot, _parent.local_chain().config() }.chunk_id() >= *target_chunk_id)
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

        void _add_chunk(uint8_vector uncompressed, std::optional<uint8_vector> compressed={})
        {
            const auto chunk_offset = _next_chunk_offset;
            _next_chunk_offset += uncompressed.size();
            _parent.local_chain().sched().submit("parse", 100, [this, uncompressed=std::move(uncompressed), compressed=std::move(compressed), chunk_offset=chunk_offset]() mutable {
                const auto ex_ptr = logger::run_log_errors([&] {
                    if (!compressed)
                        compressed.emplace(zstd::compress(uncompressed, 21));
                    auto progress = _parent.local_chain().add_buffer(chunk_offset, std::move(uncompressed), std::move(*compressed));
                    _report_chunk_download_progress(progress);
                });
                if (ex_ptr) [[unlikely]] {
                    _record_invalid_offset(chunk_offset);
                }
            });
        }

        void _add_last_chunk_if_not_empty()
        {
            if (!_last_chunk.empty()) {
                _add_chunk(std::move(_last_chunk));
                _last_chunk.clear();
            }
        }

        void _add_block(const block_container &blk)
        {
            const auto blk_slot = _parent.local_chain().make_slot(blk->slot());
            const auto last_block_slot = _last_block_slot.load(std::memory_order_relaxed);
            if (last_block_slot && *last_block_slot > blk_slot) [[unlikely]]
                throw error(fmt::format("unexpected block order: block with slot {} comes after slot {}", blk_slot, cardano::slot { *last_block_slot, _parent.local_chain().config() }));
            _parent.local_chain().report_progress("download", { blk_slot, blk.end_offset() });
            if (!last_block_slot || cardano::slot { *last_block_slot, _parent.local_chain().config() }.chunk_id() != blk_slot.chunk_id()) {
                logger::info("block from a new chunk: slot: {} hash: {} height: {}", blk_slot, blk->hash(), blk->height());
                _add_last_chunk_if_not_empty();
            }
            _record_last_block_slot(blk_slot);
            _last_chunk << blk.raw();
        }
    };

    syncer::syncer(chunk_registry &cr, peer_selection &ps, client_manager &ccm)
        : sync::syncer { cr, ps }, _impl { std::make_unique<impl>(*this, ccm) }
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
