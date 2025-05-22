/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/storage/partition.hpp>

namespace daedalus_turbo::storage {
    vector<partition> partition_map::_chunk_partitions(const chunk_registry &cr, const size_t num_parts)
    {
        vector<partition> parts {};
        partition::storage_type chunks {};
        uint64_t part_size = 0;
        for (const auto &[chunk_last_byte, chunk]: cr.chunks()) {
            const auto part_edge = cr.num_bytes() * (parts.size() + 1) / num_parts;
            const auto potential_size = part_size + chunk.data_size;
            if (chunk_last_byte < part_edge) [[likely]] {
                chunks.emplace_back(&chunk);
                part_size = potential_size;
            } else {
                const auto excess = potential_size - part_edge;
                const auto lack = part_edge - part_size;
                if (chunks.empty() || lack > excess) {
                    chunks.emplace_back(&chunk);
                    parts.emplace_back(std::move(chunks));
                    chunks.clear();
                    part_size = 0;
                } else {
                    parts.emplace_back(std::move(chunks));
                    chunks.clear();
                    chunks.emplace_back(&chunk);
                    part_size = chunk.data_size;
                }
            }
        }
        if (!chunks.empty())
            parts.emplace_back(std::move(chunks));
        if (parts.size() > num_parts) [[unlikely]]
            throw error(fmt::format("invariant failed: the number of actual partitions: {} is greater than requested: {}", parts.size(), num_parts));
        return parts;
    }

    vector<partition> epoch_partition_map::_make_partitions(const chunk_registry &cr)
    {
        vector<partition> parts {};
        partition::storage_type chunks {};
        std::optional<uint64_t> part_epoch {};
        for (const auto &[chunk_last_byte, chunk]: cr.chunks()) {
            const auto chunk_epoch = cr.make_slot(chunk.last_slot).epoch();
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

    vector<partition> chunk_partition_map::_make_partitions(const chunk_registry &cr)
    {
        vector<partition> parts {};
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
            if (from_slot && *from_slot >= chunk.last_slot)
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
            sched.submit_void("parse-chunk", -static_cast<int64_t>(part_no), [&, part_no] {
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