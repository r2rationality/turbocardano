#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/index/common.hpp>

namespace turbo::index::txo_use {

    struct item {
        cardano::tx_hash hash;
        cardano::tx_out_idx out_idx;
        uint64_t offset = 0;
        cardano::tx_size size {};
        
        bool operator<(const auto &b) const
        {
            int cmp = memcmp(hash.data(), b.hash.data(), hash.size());
            if (cmp != 0) return cmp < 0;
            if (out_idx != b.out_idx) return out_idx < b.out_idx;
            return offset < b.offset;
        }

        bool index_less(const item &b) const
        {
            int cmp = memcmp(hash.data(), b.hash.data(), hash.size());
            if (cmp != 0) return cmp < 0;
            return out_idx < b.out_idx;
        }

        bool operator==(const item &b) const
        {
            int cmp = memcmp(hash.data(), b.hash.data(), hash.size());
            if (cmp != 0) return false;
            return out_idx == b.out_idx;
        }
    };

    struct chunk_indexer: chunk_indexer_multi_part<item> {
        using chunk_indexer_multi_part<item>::chunk_indexer_multi_part;
    protected:
        void index_tx(const cardano::tx_base &tx) override
        {
            tx.foreach_input([&](const auto &txi) {
                _idx.emplace_part(txi.hash[0] / _part_range, txi.hash, txi.idx, tx.offset(), tx.size());
            });
        }

        void index_invalid_tx(const cardano::tx_base &tx) override
        {
            tx.foreach_collateral([&](const auto &txi) {
                _idx.emplace_part(txi.hash[0] / _part_range, txi.hash, txi.idx, tx.offset(), tx.size());
            });
        }
    };

    using indexer = indexer_offset<item, chunk_indexer>;
}

namespace fmt {
    template<>
    struct formatter<turbo::index::txo_use::item>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "hash: {} out_idx: {} offset: {} size: {}",
                v.hash, static_cast<size_t>(v.out_idx), static_cast<uint64_t>(v.offset), static_cast<size_t>(v.size));
        }
    };
}