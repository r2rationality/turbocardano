/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/storage/partition.hpp>

namespace turbo::storage {
    partition_map::storage_type partition_map::_chunk_partitions(const chunk_registry &cr, const config_t &cfg)
    {
        if (!cfg.num_parts)
            throw error("the number of partitions must be greater than zero");
        if (cfg.first_epoch && cfg.last_epoch && *cfg.first_epoch > *cfg.last_epoch)
            throw error("invalid epoch range: {}-{}", *cfg.first_epoch, *cfg.last_epoch);
        partition::storage_type selected {};
        uint64_t total_size = 0;
        for (const auto &[offset, chunk]: cr.chunks()) {
            const auto epoch = cr.make_slot(chunk.first_slot).epoch();
            if (cfg.first_epoch && epoch < *cfg.first_epoch)
                continue;
            if (cfg.last_epoch && epoch > *cfg.last_epoch)
                continue;
            selected.emplace_back(&chunk);
            total_size += chunk.data_size;
        }
        storage_type parts {};
        partition::storage_type chunks {};
        uint64_t consumed_size = 0;
        for (const auto *chunk: selected) {
            // Cumulative boundaries use only selected bytes; distribute the remainder without multiplying total_size.
            const auto part_no = parts.size() + 1;
            const auto part_edge = total_size / cfg.num_parts * part_no
                + std::min<uint64_t>(total_size % cfg.num_parts, part_no);
            const auto next_size = consumed_size + chunk->data_size;
            if (!chunks.empty() && part_no < cfg.num_parts
                    && (consumed_size >= part_edge
                        || (next_size > part_edge && part_edge - consumed_size < next_size - part_edge))) {
                parts.emplace_back(std::move(chunks));
                chunks.clear();
            }
            chunks.emplace_back(chunk);
            consumed_size = next_size;
        }
        if (!chunks.empty())
            parts.emplace_back(std::move(chunks));
        return parts;
    }

    std::vector<partition> epoch_partition_map::_make_partitions(const chunk_registry &cr)
    {
        std::vector<partition> parts {};
        partition::storage_type chunks {};
        std::optional<uint64_t> part_epoch {};
        for (const auto &[chunk_last_byte, chunk]: cr.chunks()) {
            const auto chunk_epoch = cr.make_slot(chunk.last_block.slot).epoch();
            if (part_epoch && *part_epoch != chunk_epoch) {
                parts.emplace_back(std::move(chunks));
                chunks.clear();
                part_epoch.reset();
            }
            if (!part_epoch)
                part_epoch.emplace(chunk_epoch);
            chunks.emplace_back(&chunk);
        }
        if (!chunks.empty())
            parts.emplace_back(std::move(chunks));
        return parts;
    }

    std::vector<partition> chunk_partition_map::_make_partitions(const chunk_registry &cr)
    {
        std::vector<partition> parts {};
        for (const auto &[chunk_last_byte, chunk]: cr.chunks()) {
            partition::storage_type chunks {};
            chunks.emplace_back(&chunk);
            parts.emplace_back(std::move(chunks));
        }
        return parts;
    }

    chunk_range_partition_map::storage_type chunk_range_partition_map::_make_partitions(const chunk_registry &cr,
        const std::optional<uint64_t> from_slot, const std::optional<uint64_t> to_slot)
    {
        storage_type parts {};
        for (const auto &[last_byte, chunk]: cr.chunks()) {
            if (from_slot && *from_slot >= chunk.last_block.slot)
                continue;
            if (to_slot && *to_slot < chunk.first_slot)
                continue;
            partition::storage_type part {};
            part.emplace_back(&chunk);
            parts.emplace_back(std::move(part));
        }
        return parts;
    }

    void parse_parallel(const chunk_registry &cr, const partition_map &pm,
        const std::function<void(std::any &, const cardano::block_base &blk)> &on_block,
        const std::function<std::any(size_t, const partition &)> &on_part_init,
        const std::function<void(std::any &&, size_t, const partition &)> &on_part_done,
        const std::optional<std::string> &progress_tag)
    {
        std::optional<progress_guard> pg {};
        if (progress_tag)
            pg.emplace({ *progress_tag });
        const uint64_t total_size = cr.num_bytes();
        std::atomic_size_t parsed_size { 0 };
        auto &sched = cr.sched();
        for (size_t part_no = 0; part_no < pm.size(); ++part_no) {
            sched.submit("parse-chunk", -static_cast<int64_t>(part_no), [&, part_no] {
                const auto &part = pm.at(part_no);
                auto tmp = on_part_init(part_no, part);
                for (const auto *chunk: part) {
                    auto canon_path = cr.full_path(chunk->rel_path());
                    const auto data = file::read(canon_path);
                    cbor::zero2::decoder dec { data };
                    while (!dec.done()) {
                        auto &block_tuple = dec.read();
                        const cardano::block_container blk { numeric_cast<uint64_t>(chunk->offset + block_tuple.data_begin() - data.data()), block_tuple, cr.config() };
                        try {
                            on_block(tmp, *blk);
                        } catch (const std::exception &ex) {
                            throw error(fmt::format("failed to parse block at slot: {} hash: {}: {}", blk->slot(), blk->hash(), ex.what()));
                        }
                    }
                }
                try {
                    on_part_done(std::move(tmp), part_no, part);
                } catch (const std::exception &ex) {
                    throw error(fmt::format("failed to complete partition [{}:{}]: {}", part.offset(), part.end_offset(), ex.what()));
                }
                if (progress_tag) {
                    const auto done = parsed_size.fetch_add(part.size(), std::memory_order::relaxed) + part.size();
                    auto &p = progress::get();
                    p.update(*progress_tag, done, total_size);
                    p.inform();
                }
            });
        }
        sched.process();
    }

    void parse_parallel(const chunk_registry &cr, const size_t num_parts,
        const std::function<void(std::any &, const cardano::block_base &blk)> &on_block,
        const std::function<std::any(size_t, const partition &)> &on_part_init,
        const std::function<void(std::any &&, size_t, const partition &)> &on_part_done,
        const std::optional<std::string> &progress_tag)
    {
        const partition_map pm { cr, num_parts };
        parse_parallel(cr, pm, on_block, on_part_init, on_part_done, progress_tag);
    }

    void parse_parallel_epoch(const chunk_registry &cr,
        const std::function<void(std::any &, const cardano::block_base &blk)> &on_block,
        const std::function<std::any(size_t, const partition &)> &on_part_init,
        const std::function<void(std::any &&, size_t, const partition &)> &on_part_done,
        const std::optional<std::string> &progress_tag)
    {
        const epoch_partition_map pm { cr };
        parse_parallel(cr, pm, on_block, on_part_init, on_part_done, progress_tag);
    }
}
