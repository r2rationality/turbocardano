#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/index/common.hpp>

namespace turbo::index::stake_ref {

    struct item {
        cardano::stake_ident id;
        uint64_t offset = 0;
        cardano::tx_size size {};
        cardano::tx_out_idx out_idx {};

        bool operator<(const auto &b) const
        {
            int cmp = memcmp(&id, &b.id, sizeof(id));
            if (cmp != 0) return cmp < 0;
            if (offset != b.offset) return offset < b.offset;
            return out_idx < b.out_idx;
        }

        bool index_less(const auto &b) const
        {
            return memcmp(&id, &b.id, sizeof(id)) < 0;
        }

        bool operator==(const auto &b) const
        {
            return memcmp(&id, &b.id, sizeof(id)) == 0;
        }
    };

    struct chunk_indexer: chunk_indexer_multi_part<item> {
        using chunk_indexer_multi_part<item>::chunk_indexer_multi_part;
    protected:
        void index_tx(const cardano::tx_base &tx) override
        {
            size_t out_idx = 0;
            tx.foreach_output([&](const auto &tx_out) {
                const auto addr = tx_out.addr();
                if (addr.has_stake_id()) {
                    const auto id = addr.stake_id();
                    _idx.emplace_part(id.hash.data()[0] / _part_range, std::move(id), tx.offset(), tx.size(), out_idx);
                }
                ++out_idx;
            });
        }
    };

    using indexer = indexer_offset<item, chunk_indexer>;
}

namespace fmt {
    template<>
    struct formatter<turbo::index::stake_ref::item>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "stake-ref::item(id: {}, offset: {}, size: {}, out_idx: {})",
                v.id, static_cast<uint64_t>(v.offset), static_cast<size_t>(v.size), static_cast<size_t>(v.out_idx));
        }
    };
}