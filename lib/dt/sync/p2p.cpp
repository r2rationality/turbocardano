/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <algorithm>
#include <dt/cardano.hpp>
#include <dt/cardano/common/network.hpp>
#include <dt/sync/p2p.hpp>
#include <dt/chunk-registry.hpp>

namespace daedalus_turbo::sync::p2p {
    using namespace daedalus_turbo::cardano::network;
    using namespace daedalus_turbo::cardano;

    struct syncer::impl {
        impl(syncer &parent, client_manager &cm)
            : _parent { parent }, _client_manager { cm }, _raw_dir { _parent.local_chain().data_dir() / "raw" }
        {
            std::filesystem::create_directories(_raw_dir);
        }

        // Finds a peer and the best intersection point
        [[nodiscard]] std::shared_ptr<sync::peer_info> find_peer(std::optional<cardano::network::address> addr) const
        {
            if (!addr)
                addr = _parent.peer_list().next_cardano();
            // Try to fit into a single packet of 1.5KB
            auto client = _client_manager.connect(*addr);
            if (_parent.local_chain().num_chunks() == 0) {
                auto tip = client->find_tip_sync();
                return std::make_shared<peer_info>(std::move(client), std::move(tip));
            }

            // iteratively determine the chunk containing the intersection point
            auto first_chunk_it = _parent.local_chain().chunks().begin();
            auto last_chunk_it = _parent.local_chain().chunks().end();
            static constexpr size_t points_per_query = 24;
            for (uint64_t chunk_dist = std::distance(first_chunk_it, last_chunk_it); chunk_dist > 1; chunk_dist = std::distance(first_chunk_it, last_chunk_it)) {
                const auto step_size = std::max(static_cast<uint64_t>(1), chunk_dist / points_per_query);
                point_list points {};
                for (auto chunk_it = first_chunk_it; chunk_it != last_chunk_it; std::ranges::advance(chunk_it, step_size, last_chunk_it)) {
                    points.emplace_back(chunk_it->second.first_block_hash(), chunk_it->second.first_slot);
                }
                // ensure that the last chunk is always present
                if (points.back().hash != _parent.local_chain().last_chunk()->first_block_hash())
                    points.emplace_back(_parent.local_chain().last_chunk()->first_block_hash(), _parent.local_chain().last_chunk()->first_slot);

                // points must be in the reverse order
                std::reverse(points.begin(), points.end());
                const auto intersection = client->find_intersection_sync(points);
                if (!intersection.isect)
                    return std::make_shared<peer_info>(std::move(client), std::move(intersection.tip));

                first_chunk_it = _parent.local_chain().find_slot_it(intersection.isect->slot);
                last_chunk_it = first_chunk_it;
                std::ranges::advance(last_chunk_it, step_size, _parent.local_chain().chunks().end());
            }
            if (std::distance(first_chunk_it, last_chunk_it) != 1)
                throw error("internal error: wasn't able to find a chunk for the intersection point!");

            // determine the block of the intersection point
            point_list points {};
            for (const auto &block: first_chunk_it->second.blocks)
                points.emplace_back(block.hash, block.slot, block.height, block.end_offset());
            std::ranges::reverse(points);
            const auto intersection = client->find_intersection_sync(points);
            if (!intersection.isect)
                throw error("internal error: wasn't able to narrow down the intersection point to a block!");
            return std::make_shared<peer_info>(std::move(client), std::move(intersection.tip),
                _parent.local_chain().find_block_by_slot(intersection.isect->slot, intersection.isect->hash).point());
        }

        void sync_attempt(peer_info &peer, const cardano::optional_slot max_slot)
        {
            // target offset is unbounded since the cardano network protocol does not provide the size information
            _invalid_first_offset = std::optional<uint64_t> {};
            _next_chunk_offset = 0;
            if (peer.intersection())
                _next_chunk_offset = _parent.local_chain().find_block_by_slot(peer.intersection()->slot, peer.intersection()->hash).end_offset();
            _sync(peer, peer.intersection(), max_slot);
            _save_last_chunk();
            _parent.local_chain().sched().process();
        }

        void cancel_tasks(const uint64_t max_valid_offset)
        {
            mutex::scoped_lock lk { _invalid_mutex };
            const auto last_val = _invalid_first_offset.load();
            if (!last_val || *last_val > max_valid_offset) {
                _invalid_first_offset = max_valid_offset;
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
        uint8_vector _last_chunk {};
        std::optional<uint64_t> _last_chunk_id {};
        uint64_t _next_chunk_offset = 0;
        mutex::unique_lock::mutex_type _invalid_mutex alignas(mutex::alignment) {};
        std::atomic<std::optional<uint64_t>> _invalid_first_offset {};

        void _sync(peer_info &peer, const std::optional<point> &local_tip, const std::optional<uint64_t> &max_slot)
        {
            std::optional<point> continue_from = local_tip;
            const auto [headers, tip] = peer.client().fetch_headers_sync(continue_from, 1, true);
            if (!headers.empty() && (!max_slot || headers.front().slot <= *max_slot)) {
                // current implementation of fetch_blocks does not leave its connection in a working state
                std::optional<std::string> err {};
                peer.client().fetch_blocks(headers.front(), tip, [&](auto &&resp) {
                    if (resp.err) {
                        err = std::move(*resp.err);
                        return false;
                    }
                    if (_invalid_first_offset.load() || (max_slot && resp.block->blk->slot() > *max_slot))
                        return false;
                    _add_block(resp.block->blk);
                    return true;
                });
                peer.client().process(&_parent.local_chain().sched());
                if (err)
                    throw error(fmt::format("fetch_block has failed with error: {}", err));
            }
        }

        void _save_last_chunk()
        {
            if (!_last_chunk.empty()) {
                const auto chunk_data = std::make_shared<uint8_vector>(std::move(_last_chunk));
                const auto chunk_name = fmt::format("{:05d}.zstd", *_last_chunk_id);
                const auto chunk_path = (_raw_dir / chunk_name).string();
                const auto chunk_offset = _next_chunk_offset;
                _last_chunk.clear();
                const auto max_valid = _invalid_first_offset.load();
                if (!max_valid || chunk_offset + chunk_data->size() <= *max_valid) {
                    _parent.local_chain().sched().submit_void("parse", 100, [this, chunk_offset, chunk_data, chunk_path] {
                        zstd::write(chunk_path, *chunk_data);
                        _parent.local_chain().add(chunk_offset, chunk_path);
                    });
                    _next_chunk_offset += chunk_data->size();
                }
            }
        }

        void _add_block(const block_container &blk)
        {
            const auto blk_slot = _parent.local_chain().make_slot(blk->slot());
            const auto blk_chunk_id = blk_slot.chunk_id();
            _parent.local_chain().report_progress("download", { blk_slot, blk.end_offset() });
            if (!_last_chunk_id || _last_chunk_id != blk_chunk_id) {
                logger::info("block from a new chunk: slot: {} hash: {} height: {}", blk_slot, blk->hash(), blk->height());
                _save_last_chunk();
                _last_chunk_id = blk_chunk_id;
            }
            _last_chunk << blk.raw();
        }
    };

    syncer::syncer(chunk_registry &cr, peer_selection &ps, client_manager &ccm)
        : sync::syncer { cr, ps }, _impl { std::make_unique<impl>(*this, ccm) }
    {
    }

    syncer::~syncer() =default;

    [[nodiscard]] std::shared_ptr<sync::peer_info> syncer::find_peer(std::optional<cardano::network::address> addr) const
    {
        return _impl->find_peer(addr);
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