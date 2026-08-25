#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <string>
#include <turbo/cardano/common/common.hpp>
#include <turbo/common/format.hpp>

namespace turbo::cardano {
    struct block_container;
}

namespace turbo::storage {
    using block_info = cardano::block_info;
    static_assert(sizeof(block_info) == 88);
    using block_list = std::vector<block_info>;

    struct chunk_info {
        size_t data_size = 0;
        size_t compressed_size = 0;
        int32_t compression_level = 0; // zero means unknown
        size_t num_blocks = 0; // needed when chunk info is serialized without the blocks fields
        uint64_t first_slot = 0;
        uint64_t last_slot {};
        cardano::block_hash data_hash {};
        cardano::block_hash prev_block_hash {};
        cardano::block_hash last_block_hash {};
        uint64_t offset = 0;
        // fields that are not serialized to json:
        block_list blocks {};

        constexpr static auto serialize(auto &archive, auto &self)
        {
            return archive(
                self.data_size, self.compressed_size,
                self.num_blocks, self.first_slot, self.last_slot,
                self.data_hash, self.prev_block_hash, self.last_block_hash,
                self.offset, self.blocks
            );
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
            if (j.contains("offset"))
                chunk.offset = json::value_to<size_t>(j.at("offset"));
            chunk.data_size = json::value_to<size_t>(j.at("size"));
            chunk.compressed_size = json::value_to<size_t>(j.at("compressedSize"));
            if (j.contains("compressionLevel"))
                chunk.compression_level = json::value_to<int32_t>(j.at("compressionLevel"));
            chunk.num_blocks = json::value_to<size_t>(j.at("numBlocks"));
            chunk.first_slot = json::value_to<uint64_t>(j.at("firstSlot"));
            chunk.last_slot = json::value_to<uint64_t>(j.at("lastSlot"));
            chunk.data_hash = decltype(chunk.data_hash)::from_hex(json::value_to<std::string_view>(j.at("hash")));
            chunk.prev_block_hash = decltype(chunk.prev_block_hash)::from_hex(json::value_to<std::string_view>(j.at("prevBlockHash")));
            chunk.last_block_hash = decltype(chunk.last_block_hash)::from_hex(json::value_to<std::string_view>(j.at("lastBlockHash")));
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
                { "lastSlot", static_cast<uint64_t>(last_slot) },
                { "hash", fmt::format("{}", data_hash) },
                { "prevBlockHash", fmt::format("{}", prev_block_hash) },
                { "lastBlockHash", fmt::format("{}", last_block_hash) }
            };
        }
    };
    using chunk_list = std::vector<chunk_info>;
    using chunk_cptr_list = std::vector<const chunk_info *>;
    using chunk_map = std::map<uint64_t, chunk_info>;
}
