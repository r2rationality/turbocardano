#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <mutex>
#include <turbo/chunk-registry.hpp>
#include "chunk-cache.hpp"

namespace turbo::cardano::network {
    // Published metadata is immutable. Chunk files are pinned independently of
    // the mutable registry, including across asynchronous BlockFetch responses.
    struct chain_source {
        struct chunk {
            storage::chunk_info info;
            std::string path;
            std::shared_ptr<void> pin;
        };
        struct view {
            explicit view(const config &cfg): configuration { cfg } {}
            const config configuration;
            using position = std::pair<size_t, size_t>;
            std::vector<std::shared_ptr<const chunk>> chunks;
            optional_point tip;
            optional_point intersection;
            uint64_t generation = 0;

            std::optional<position> find(const point2 &p) const
            {
                auto it = std::lower_bound(chunks.begin(), chunks.end(), p.slot,
                    [](const auto &c, uint64_t slot) { return c->info.last_block.slot < slot; });
                for (; it != chunks.end() && (*it)->info.first_slot <= p.slot; ++it) {
                    const auto &blocks = (*it)->info.blocks;
                    auto b = std::lower_bound(blocks.begin(), blocks.end(), p.slot,
                        [](const auto &blk, uint64_t slot) { return blk.slot < slot; });
                    for (; b != blocks.end() && b->slot == p.slot; ++b)
                        if (b->hash == p.hash)
                            return position { it - chunks.begin(), b - blocks.begin() };
                }
                return {};
            }

            position end() const { return { chunks.size(), 0 }; }
            const storage::block_info &block(position p) const { return chunks.at(p.first)->info.blocks.at(p.second); }
            position next(position p) const
            {
                if (p == end()) return p;
                if (++p.second == chunks[p.first]->info.blocks.size()) { ++p.first; p.second = 0; }
                return p;
            }
            std::optional<position> previous(position p) const
            {
                if (p.second) return position { p.first, p.second - 1 };
                if (p.first) return position { p.first - 1, chunks[p.first - 1]->info.blocks.size() - 1 };
                return {};
            }
        };

        explicit chain_source(std::shared_ptr<chunk_registry> cr, size_t cache_bytes=chunk_cache::default_max_bytes):
            _cr { std::move(cr) }, _cache { cache_bytes }
        {
            publish({});
        }

        std::shared_ptr<const view> current() const
        {
            std::scoped_lock lk { _mutex };
            return _view;
        }

        chunk_cache::data_ptr read_chunk(const chunk &c)
        {
            if (c.info.data_size > zstd::max_zstd_buffer)
                throw error("chunk exceeds the maximum decompressed size");
            return _cache.get(c.info.data_hash, c.info.data_size, [&](write_buffer bytes) {
                const auto compressed = file::read(c.path);
                zstd::decompress(bytes, compressed);
            });
        }

        parsed_header read_header(const view &v, const view::position p)
        {
            const auto &c = v.chunks.at(p.first);
            const auto bytes = read_chunk(*c);
            const auto &b = v.block(p);
            return { b.era, static_cast<buffer>(*bytes).subbuf(
                b.offset + b.header_offset + 1 - c->info.offset, b.header_size), v.configuration };
        }

        // Called by the sole chain writer after all validation and commit work.
        void publish(const optional_point &intersection)
        {
            // The live writer bounds fragmentation before publishing the new view.
            // Existing readers retain pins on the files in their previous views.
            if (_cr->continuous())
                _cr->repack(chunk_registry::repack_mode_t::merge_fragmented, 3);
            const auto old = current();
            auto next = std::make_shared<view>(_cr->config());
            next->tip = _cr->tip();
            next->intersection = intersection;
            next->generation = old ? old->generation + 1 : 0;
            size_t i = 0;
            for (const auto &[offset, c]: _cr->chunks()) {
                if (!next->tip || c.end_offset() > next->tip->end_offset) break;
                if (old && i < old->chunks.size() && old->chunks[i]->info.data_hash == c.data_hash
                        && old->chunks[i]->info.offset == c.offset) {
                    next->chunks.emplace_back(old->chunks[i]);
                } else {
                    const auto path = _cr->full_path(c.rel_path());
                    next->chunks.emplace_back(std::make_shared<chunk>(chunk { c, path, _cr->remover().pin(path) }));
                }
                ++i;
            }
            std::scoped_lock lk { _mutex };
            _view = std::move(next);
        }
    private:
        std::shared_ptr<chunk_registry> _cr;
        chunk_cache _cache;
        mutable std::mutex _mutex;
        std::shared_ptr<const view> _view;
    };
}
