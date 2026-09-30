/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <algorithm>
#include <condition_variable>
#include <limits>
#include <turbo/cardano.hpp>
#include <turbo/cardano/network/common.hpp>
#include <turbo/cbor/zero2.hpp>
#include <turbo/chunk-registry.hpp>
#include <turbo/sync/p2p.hpp>
#include <turbo/common/scope-exit.hpp>

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

        std::shared_ptr<lease> acquire(const uint64_t amount, const std::stop_token stop={})
        {
            std::unique_lock lk { _mutex };
            _cv.wait(lk, stop, [&] {
                // Always admit one oversized item when the budget is otherwise empty.
                return storage::chunk_work_policy_t::can_admit(_used, amount, _limit);
            });
            if (stop.stop_requested())
                throw error("sync memory-budget wait stopped");
            _used += amount;
            return std::make_shared<lease>(shared_from_this(), amount);
        }
    private:
        std::mutex _mutex {};
        std::condition_variable_any _cv {};
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
        [[nodiscard]] std::shared_ptr<sync::peer_info> find_peer(std::optional<network::address> addr,
            const version_config_t &versions, const std::stop_token stop={}) const
        {
            if (!addr)
                addr = _parent.peer_list().next_cardano();
            logger::info("connecting to peer {} requesting versions [{};{}]", *addr, versions.min, versions.max);
            auto client = _client_manager.connect(*addr, versions, _parent.local_chain().config());
            client->set_stop_token(stop);
            // Try to fit into a single packet of 1460 bytes : 37 * (33 + 5) + message header + segment header (8 bytes)
            static constexpr ptrdiff_t points_per_query = 37;
            auto first_it = _parent.local_chain().cbegin();
            auto last_it = _parent.local_chain().cend();
            std::optional<optional_point3> tip {};
            while (last_it - first_it > 1) {
                const auto distance = last_it - first_it;
                const auto step = std::max(ptrdiff_t { 1 }, distance / points_per_query);

                optional_point2_list points {};
                for (auto it = first_it; it != last_it; it = it + std::min(step, last_it - it)) {
                    points.emplace_back(it->slot, it->hash);
                }
                std::ranges::reverse(points);
                const auto intersection = client->find_intersection_sync(points);
                if (!intersection.isect)
                    return std::make_shared<peer_info>(std::move(client), intersection.tip);

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
            if (!*tip || first_it == _parent.local_chain().cend())
                return std::make_shared<peer_info>(std::move(client), *tip);
            return std::make_shared<peer_info>(std::move(client), *tip, first_it->point());
        }

        bool sync_attempt(peer_info &peer, const cardano::optional_slot max_slot, const std::stop_token stop={})
        {
            // target offset is unbounded since the cardano network protocol does not provide the size information
            _invalid_first_offset.store(no_recorded_value, std::memory_order_relaxed);
            _next_chunk_offset = 0;
            _last_chunk.clear();
            _last_chunk_id.reset();
            _last_block_slot.store(no_recorded_value, std::memory_order_relaxed);
            _slot_limit_reached.store(false, std::memory_order_relaxed);

            if (peer.intersection())
                _next_chunk_offset = _parent.local_chain().get_block_info(*peer.intersection()).end_offset();
            _sync(peer, peer.intersection(), max_slot, stop);
            return _slot_limit_reached.load(std::memory_order_relaxed)
                && _invalid_first_offset.load(std::memory_order_relaxed) == no_recorded_value;
        }

        void follow(const std::stop_token stop, const std::function<void(const optional_point &)> &publish,
            const std::optional<network::address> &addr, const version_config_t &versions,
            const std::chrono::seconds checkpoint_interval)
        {
            auto &cr = _parent.local_chain();
            if (!cr.continuous() || checkpoint_interval.count() <= 0)
                throw error("follow requires a continuous registry and a positive checkpoint interval");
            const auto previous_mode = cr.validation(validator::validation_mode::full);
            scope_exit restore_mode { [&] {
                if (!cr.tx())
                    cr.validation(previous_mode);
            }};
            auto last_checkpoint = std::chrono::steady_clock::now();
            auto checkpoint_tip = cr.tip();
            if (cr.checkpoint(false).partial_groups_merged)
                publish(cr.tip());
            const auto maybe_checkpoint = [&] {
                const auto now = std::chrono::steady_clock::now();
                if (cr.tip() == checkpoint_tip || now - last_checkpoint < checkpoint_interval) return;
                last_checkpoint = now;
                try {
                    if (cr.checkpoint(false).partial_groups_merged)
                        publish(cr.tip());
                    checkpoint_tip = cr.tip();
                } catch (const std::exception &ex) {
                    logger::error("live checkpoint failed (will retry after the interval): {}", ex.what());
                }
            };
            while (!stop.stop_requested()) {
                try {
                    auto base_peer = find_peer(addr, versions, stop);
                    auto &peer = dynamic_cast<peer_info &>(*base_peer);
                    optional_point2_list points;
                    if (peer.intersection()) points.emplace_back(*peer.intersection());
                    points.emplace_back(); // Origin is always a valid intersection.
                    const auto intersection = peer.client().find_intersection_sync(points);
                    if (!intersection.found) throw error("upstream failed to intersect at origin");
                    if (!intersection.tip && cr.tip())
                        throw error("empty upstream cannot replace a populated local chain");
                    optional_point isect;
                    if (intersection.isect) isect = cr.get_block_info(*intersection.isect).point();
                    auto highest_tip = cr.tip();
                    std::chrono::steady_clock::duration peer_wait {};
                    if (isect != cr.tip()) {
                        cr.truncate(isect);
                        publish(isect);
                    }
                    while (!stop.stop_requested()) {
                        header_list headers;
                        optional_point3 remote_tip {};
                        bool bulk = false;
                        // Bound each transaction. At the tip, a single block is
                        // processed immediately instead of waiting for a full batch.
                        while (headers.size() < 256 && !stop.stop_requested()) {
                            auto wait_started = std::chrono::steady_clock::now();
                            const auto check_progress = [&] {
                                if (peer_wait + (std::chrono::steady_clock::now() - wait_started) >= std::chrono::seconds { 30 })
                                    throw error("peer supplied no advancing chain after 30 seconds waiting for headers");
                            };
                            const auto update = peer.client().next_header_sync(stop, [&] {
                                const auto checkpoint_started = std::chrono::steady_clock::now();
                                maybe_checkpoint();
                                wait_started += std::chrono::steady_clock::now() - checkpoint_started;
                                check_progress();
                            });
                            check_progress();
                            peer_wait += std::chrono::steady_clock::now() - wait_started;
                            remote_tip = update.tip;
                            if (update.rollback) {
                                auto buffered = update.point ? std::ranges::find(headers, *update.point) : headers.end();
                                if (buffered != headers.end()) {
                                    headers.erase(std::next(buffered), headers.end());
                                } else {
                                    headers.clear();
                                    optional_point target;
                                    if (update.point) target = cr.get_block_info(*update.point).point();
                                    if (target != cr.tip()) {
                                        cr.truncate(target);
                                        publish(target);
                                    }
                                }
                                if (!headers.empty() && remote_tip && headers.back().hash == remote_tip->hash) break;
                                continue;
                            }
                            if (!update.point) throw error("RollForward without a header");
                            if (!remote_tip) throw error("RollForward with a tip at origin");
                            headers.emplace_back(*update.point);
                            const auto local_height = cr.tip() ? cr.tip()->height : 0;
                            if (headers.size() == 1 && remote_tip.height > local_height + 256) {
                                // Preserve the existing parallel range import for catch-up.
                                // Its terminal hash is fixed by this announcement.
                                bulk = true;
                                break;
                            }
                            if (headers.back().hash == remote_tip->hash) break;
                        }
                        if (headers.empty() || stop.stop_requested()) break;
                        const auto start = cr.tip();
                        point2 target = bulk ? remote_tip.value() : headers.back();
                        peer.intersection(start);
                        std::exception_ptr failure;
                        bool checkpoint_requested = false;
                        {
                            scope_exit clear { [&] { cr.before_commit({}); _follow_range.reset(); } };
                            _follow_range = std::make_pair(headers.front(), target);
                            progress_point progress_target { target.slot };
                            progress_target.height = remote_tip.height;
                            failure = _parent.accept_progress(start, progress_target, [&] {
                                sync_attempt(peer, target.slot, stop);
                                // Commit may clear the request after saving the checkpoint.
                                checkpoint_requested = cr.checkpoint_requested();
                                cr.before_commit([&] {
                                    if (stop.stop_requested()) return;
                                    const auto tip = cr.tip();
                                    if (!tip || (tip->hash != target.hash && !cr.checkpoint_requested()))
                                        throw error("block fetch did not reach the announced boundary");
                                });
                            });
                        }
                        if (const auto tip = cr.tip(); tip && (!highest_tip || tip->height > highest_tip->height)) {
                            highest_tip = tip;
                            peer_wait = {};
                        }
                        if (cr.tip() != start)
                            publish(start);
                        if (stop.stop_requested()) {
                            break;
                        }
                        maybe_checkpoint();
                        if (failure)
                            std::rethrow_exception(failure);
                        if (bulk || checkpoint_requested) {
                            target = static_cast<point2>(*cr.tip());
                            // Bulk fetches advance beyond the ChainSync cursor. A checkpoint
                            // may also interrupt a short fetch and close its connection.
                            const auto found = peer.client().find_intersection_sync(optional_point2_list { target });
                            if (!found.isect || found.isect->hash != target.hash)
                                throw error("upstream changed branch during catch-up");
                        }
                    }
                } catch (const std::exception &ex) {
                    if (stop.stop_requested()) {
                        break;
                    }
                    logger::warn("continuous sync peer failed: {}", ex.what());
                    cr.maintenance();
                    publish(cr.tip());
                    for (size_t i = 0; i < 10 && !stop.stop_requested(); ++i)
                        std::this_thread::sleep_for(std::chrono::milliseconds { 100 });
                }
            }
            logger::info("continuous sync stopped; saving final checkpoint");
            if (cr.checkpoint().partial_groups_merged)
                publish(cr.tip());
            logger::info("continuous sync final checkpoint complete");
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
        static constexpr uint64_t no_recorded_value = std::numeric_limits<uint64_t>::max();

        syncer &_parent;
        client_manager &_client_manager;
        std::filesystem::path _raw_dir;
        std::shared_ptr<inflight_budget> _inflight_budget;

        std::optional<std::pair<point2, point2>> _follow_range;
        uint8_vector _last_chunk{};
        std::optional<uint64_t> _last_chunk_id {};
        std::atomic<uint64_t> _last_block_slot { no_recorded_value };
        std::atomic_bool _slot_limit_reached { false };
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

        void _sync(peer_info &peer, const std::optional<point> &local_tip, const optional_slot max_slot,
            const std::stop_token stop)
        {
            const auto [headers, tip] = [&]() -> std::pair<header_list, optional_point2> {
                if (_follow_range)
                    return { header_list { _follow_range->first }, _follow_range->second };
                auto headers = peer.client().fetch_headers_sync(local_tip, 1, true).first;
                // Keep the range endpoint fixed across retries and advancing peer tips.
                return { std::move(headers), peer.tip() ? optional_point2 { peer.tip().value() } : optional_point2 {} };
            }();
            if (!headers.empty() && max_slot && headers.front().slot > *max_slot) {
                _slot_limit_reached.store(true, std::memory_order_relaxed);
                return;
            }
            if (!headers.empty() && (!max_slot || headers.front().slot <= *max_slot)) {
                if (!tip) throw error("cannot fetch blocks from a tip at origin");
                _parse_start_chunk_id = cardano::slot { headers.front().slot, _parent.local_chain().config() }.chunk_id();
                _parse_target_chunk_id = cardano::slot { max_slot.value_or(tip->slot), _parent.local_chain().config() }.chunk_id();
                _parse_next_chunk_id = _parse_start_chunk_id;
                // Completed batches preserve the connection; early stops close it promptly.
                std::optional<std::string> err{};
                try {
                    peer.client().fetch_blocks(headers.front(), *tip, [&](auto resp) {
                        return std::visit([&](auto &&rv) -> bool {
                            using T = std::decay_t<decltype(rv)>;
                            if constexpr (std::is_same_v<T, client::error_msg>) {
                                err = std::move(rv);
                                return false;
                            } else if constexpr (std::is_same_v<T, client::msg_block_t>) {
                                if (stop.stop_requested() || _parent.local_chain().checkpoint_requested()) return false;
                                auto blk = std::make_unique<parsed_block>(rv.bytes);
                                if (_invalid_first_offset.load(std::memory_order_relaxed) != no_recorded_value)
                                    return false;
                                if (max_slot && blk->blk->slot() > *max_slot) {
                                    _slot_limit_reached.store(true, std::memory_order_relaxed);
                                    return false;
                                }
                                _add_block(blk->blk);
                                return true;
                            } else if constexpr (std::is_same_v<T, client::msg_compressed_blocks_t>) {
                                if (stop.stop_requested() || _parent.local_chain().checkpoint_requested()) return false;
                                if (rv.encoding != T::encoding_zstd_fast && rv.encoding != T::encoding_zstd_max) [[unlikely]] {
                                    logger::error("unsupported encoding: {}", rv.encoding);
                                    return false;
                                }
                                if (_invalid_first_offset.load(std::memory_order_relaxed) != no_recorded_value
                                        || _slot_limit_reached.load(std::memory_order_relaxed))
                                    return false;
                                // Physical fragments can share a logical chunk ID. Only
                                // BatchDone completes an unrestricted range fetch.
                                const auto compression_level = rv.compression_level();
                                _add_compressed_chunk(std::move(rv.payload), compression_level,
                                    _parse_priority(_parse_next_chunk_id++), stop,
                                    max_slot && *max_slot < tip->slot ? max_slot : optional_slot {});
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
                peer.client().process(&_parent.local_chain().sched());
                _add_last_chunk_if_not_empty();
                _parent.local_chain().sched().process();
                if (err && !stop.stop_requested()) [[unlikely]]
                    throw error(fmt::format("fetch_block has failed with error: {}", err));
            }
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

        void _add_compressed_chunk(uint8_vector compressed, const int32_t compression_level, const int64_t priority,
            const std::stop_token stop, const optional_slot max_slot)
        {
            const auto uncompressed_size = zstd::decompressed_size(compressed);
            if (uncompressed_size > zstd::max_zstd_buffer) [[unlikely]]
                throw error(fmt::format("compressed chunk expands to {} bytes, exceeding the maximum of {}",
                    uncompressed_size, zstd::max_zstd_buffer));
            // A bounded import may also allocate a recompressed prefix of the decoded message.
            const auto raw_budget = max_slot ? 2 * uncompressed_size : uncompressed_size;
            auto budget_lease = _inflight_budget->acquire(storage::chunk_work_policy_t::estimated_cost(raw_budget, compressed.capacity()), stop);
            const auto chunk_offset = _next_chunk_offset;
            logger::debug("sync received a compressed chunk at offset {}: size: {}, compression ratio: {:.2f}",
                chunk_offset, uncompressed_size, compressed.empty() ? 0.0 : static_cast<double>(uncompressed_size) / compressed.size());
            _next_chunk_offset += uncompressed_size;
            auto compressed_ptr = std::make_shared<uint8_vector>(std::move(compressed));
            _parent.local_chain().sched().submit("parse", priority,
                [this, compressed=std::move(compressed_ptr), chunk_offset=chunk_offset,
                    compression_level=compression_level, budget_lease=std::move(budget_lease), max_slot]() mutable {
                    static_cast<void>(budget_lease);
                    const auto ex_ptr = logger::run_log_errors([&] {
                        if (max_slot) {
                            const auto raw = zstd::decompress(*compressed);
                            cbor::zero2::decoder dec { raw };
                            size_t keep = 0;
                            while (!dec.done()) {
                                auto &tuple = dec.read();
                                const block_container block { chunk_offset + keep, tuple, _parent.local_chain().config() };
                                if (block->slot() > *max_slot)
                                    break;
                                keep += block.raw().size();
                            }
                            if (keep < raw.size()) {
                                if (keep) {
                                    const auto prefix = static_cast<buffer>(raw).subbuf(0, keep);
                                    const auto level = client::msg_compressed_blocks_t::fast_compression_level;
                                    const auto trimmed = zstd::compress(prefix, level);
                                    const auto progress = _parent.local_chain().add_buffer_trusted(
                                        chunk_offset, prefix, trimmed, level);
                                    _report_chunk_download_progress(progress);
                                }
                                _slot_limit_reached.store(true, std::memory_order_relaxed);
                                return;
                            }
                            const auto progress = _parent.local_chain().add_buffer_trusted(
                                chunk_offset, raw, *compressed, compression_level);
                            _report_chunk_download_progress(progress);
                            return;
                        }
                        auto progress = _parent.local_chain().add_compressed(
                            chunk_offset, std::move(*compressed), compression_level);
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
            logger::debug("adding an uncompressed chunk at offset {}: raw size: {}", chunk_offset, uncompressed.size());
            _next_chunk_offset += uncompressed.size();
            const auto compressed_capacity = ZSTD_compressBound(uncompressed.size());
            auto budget_lease = _inflight_budget->acquire(storage::chunk_work_policy_t::estimated_cost(uncompressed.capacity(), compressed_capacity));
            auto uncompressed_ptr = std::make_shared<uint8_vector>(std::move(uncompressed));
            _parent.local_chain().sched().submit("parse", priority,
                [this, uncompressed=std::move(uncompressed_ptr), chunk_offset=chunk_offset,
                    budget_lease=std::move(budget_lease)]() mutable {
                    static_cast<void>(budget_lease);
                    const auto ex_ptr = logger::run_log_errors([&] {
                        auto progress = _parent.local_chain().add_buffer(
                            chunk_offset, std::move(*uncompressed), 21);
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
            if (max_inflight_bytes != auto_max_inflight_bytes) {
                return max_inflight_bytes;
            }
            return storage::chunk_work_policy_t::default_budget(cr.sched().num_workers());
        }()) }
    {
    }

    syncer::~syncer() =default;

    [[nodiscard]] std::shared_ptr<sync::peer_info> syncer::find_peer(std::optional<network::address> addr, const version_config_t &versions) const
    {
        return _impl->find_peer(addr, versions);
    }

    void syncer::follow(const std::stop_token stop, const std::function<void(const optional_point &)> &on_update,
        const std::optional<network::address> addr, const version_config_t &versions,
        const std::chrono::seconds checkpoint_interval)
    {
        _impl->follow(stop, on_update, addr, versions, checkpoint_interval);
    }

    void syncer::cancel_tasks(const uint64_t max_valid_offset)
    {
        _impl->cancel_tasks(max_valid_offset);
    }

    bool syncer::sync_attempt(sync::peer_info &peer, const cardano::optional_slot max_slot)
    {
        return _impl->sync_attempt(dynamic_cast<peer_info &>(peer), max_slot);
    }
}
