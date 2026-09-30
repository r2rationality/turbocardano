#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <turbo/cardano/common/common.hpp>
#include <turbo/common/format.hpp>
#include <turbo/common/numeric-cast.hpp>

namespace turbo::cardano {
    struct block_container;
}

namespace turbo::storage {
    struct chunk_work_policy_t {
        static constexpr size_t bytes_per_worker = size_t { 128 } << 20;
        // Estimate for block metadata, indexers, and other task-local allocations.
        // This is an admission budget, not a bound on total process memory.
        static constexpr uint64_t fixed_task_overhead = uint64_t { 32 } << 20;

        static size_t default_budget(const size_t workers)
        {
            const auto count = std::max<size_t>(1, workers);
            if (count > std::numeric_limits<size_t>::max() / bytes_per_worker) [[unlikely]] {
                throw error("chunk-work worker budget overflow");
            }
            return count * bytes_per_worker;
        }

        // Supply allocated capacities when available, otherwise expected sizes.
        static uint64_t estimated_cost(const uint64_t raw_bytes, const uint64_t compressed_bytes)
        {
            constexpr auto available = std::numeric_limits<uint64_t>::max() - fixed_task_overhead;
            if (raw_bytes > available || compressed_bytes > available - raw_bytes) [[unlikely]] {
                throw error("chunk-work cost overflow");
            }
            return raw_bytes + compressed_bytes + fixed_task_overhead;
        }

        static bool can_admit(const uint64_t used, const uint64_t cost, const uint64_t limit)
        {
            // Permit one oversized task only when the budget is empty.
            return used == 0 || (cost <= limit && used <= limit - cost);
        }
    };

    using block_info = cardano::block_info;
    static_assert(sizeof(block_info) == 88);
    using block_list = std::vector<block_info>;

    struct chunk_info {
        // Byte/slot boundaries and the endpoint occupy the first 64 bytes.
        uint64_t offset = 0;
        size_t data_size = 0;
        uint64_t first_slot = 0;
        cardano::point2 last_block {};

        // File identity and compression metadata are used together when loading.
        cardano::block_hash data_hash {};
        size_t compressed_size = 0;
        int32_t compression_level = 0; // zero means unknown
        cardano::block_hash prev_block_hash {};

        size_t num_blocks = 0; // needed when chunk info is serialized without blocks
        block_list blocks {}; // not serialized to JSON

        constexpr static auto serialize(auto &archive, auto &self)
        {
            return archive(
                self.data_size, self.compressed_size,
                self.num_blocks, self.first_slot, self.last_block.slot,
                self.data_hash, self.prev_block_hash, self.last_block.hash,
                self.offset, self.blocks
            );
        }

        [[nodiscard]] chunk_info without_blocks() const
        {
            return {
                .offset=offset,
                .data_size=data_size,
                .first_slot=first_slot,
                .last_block=last_block,
                .data_hash=data_hash,
                .compressed_size=compressed_size,
                .compression_level=compression_level,
                .prev_block_hash=prev_block_hash,
                .num_blocks=num_blocks
            };
        }

        static std::string rel_path_from_hash(const cardano::block_hash &data_hash)
        {
            return fmt::format("chunk/{}.zstd", data_hash);
        }

        [[nodiscard]] std::string rel_path() const
        {
            return rel_path_from_hash(data_hash);
        }

        [[nodiscard]] uint64_t era() const
        {
            if (!blocks.empty()) [[likely]] {
                const auto first_era = blocks.front().era;
                const auto last_era = blocks.back().era;
                if (first_era == last_era || (first_era == 0 && last_era == 1)) [[likely]]
                    return last_era;
                throw error(fmt::format("chunk {} has blocks from different eras {} and {}", rel_path(), first_era, last_era));
            }
            throw error("chunk cannot be empty!");
        }

        [[nodiscard]] const cardano::block_hash &first_block_hash() const
        {
            if (!blocks.empty()) [[likely]]
                return blocks.front().hash;
            throw error("chunk cannot be empty!");
        }

        [[nodiscard]] uint64_t block_data_size() const
        {
            uint64_t sz = 0;
            for (const auto &b: blocks)
                sz += b.size;
            return sz;
        }

        [[nodiscard]] uint64_t end_offset() const
        {
            return offset + data_size;
        }

        static chunk_info from_json(const json::object &j)
        {
            chunk_info chunk {};
            if (j.contains("offset")) {
                chunk.offset = json::value_to<uint64_t>(j.at("offset"));
            }
            chunk.data_size = json::value_to<size_t>(j.at("size"));
            chunk.compressed_size = json::value_to<size_t>(j.at("compressedSize"));
            if (j.contains("compressionLevel"))
                chunk.compression_level = json::value_to<int32_t>(j.at("compressionLevel"));
            chunk.num_blocks = json::value_to<size_t>(j.at("numBlocks"));
            chunk.first_slot = json::value_to<uint64_t>(j.at("firstSlot"));
            chunk.last_block.slot = json::value_to<uint64_t>(j.at("lastSlot"));
            chunk.data_hash = decltype(chunk.data_hash)::from_hex(json::value_to<std::string_view>(j.at("hash")));
            chunk.prev_block_hash = decltype(chunk.prev_block_hash)::from_hex(json::value_to<std::string_view>(j.at("prevBlockHash")));
            chunk.last_block.hash = decltype(chunk.last_block.hash)::from_hex(json::value_to<std::string_view>(j.at("lastBlockHash")));
            return chunk;
        }

        [[nodiscard]] json::object to_json() const
        {
            return json::object {
                { "size", data_size },
                { "compressedSize", compressed_size },
                { "compressionLevel", compression_level },
                { "numBlocks", num_blocks },
                { "firstSlot", static_cast<uint64_t>(first_slot) },
                { "lastSlot", last_block.slot },
                { "hash", fmt::format("{}", data_hash) },
                { "prevBlockHash", fmt::format("{}", prev_block_hash) },
                { "lastBlockHash", fmt::format("{}", last_block.hash) }
            };
        }
    };
    static_assert(offsetof(chunk_info, last_block) == 24);
    static_assert(offsetof(chunk_info, data_hash) == 64);
    static_assert(offsetof(chunk_info, num_blocks) == 144);
    static_assert(offsetof(chunk_info, blocks) == 152);
    static_assert(sizeof(chunk_info) == 152 + sizeof(block_list));
    using chunk_list = std::vector<chunk_info>;
    using chunk_cptr_list = std::vector<const chunk_info *>;
    using chunk_map = std::map<uint64_t, chunk_info>;

    inline bool matches_block_boundary(const chunk_map &chunks, const uint64_t end, const uint64_t slot)
    {
        if (!end) {
            return slot == 0;
        }
        const auto chunk = chunks.lower_bound(end - 1);
        if (chunk == chunks.end()) {
            return false;
        }
        const auto &blocks = chunk->second.blocks;
        const auto block = std::ranges::lower_bound(blocks, end, {}, &block_info::end_offset);
        return block != blocks.end() && block->end_offset() == end && block->slot == slot;
    }
}
