/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2024 Alex Sierkov (alex dot sierkov at gmail dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */
#ifndef DAEDALUS_TURBO_INDEX_TX_HPP
#define DAEDALUS_TURBO_INDEX_TX_HPP

#include <dt/cardano.hpp>
#include <dt/index/common.hpp>

namespace daedalus_turbo::index::tx {
    struct item {
        cardano::tx_hash hash {};
        uint64_t offset: 63 = 0;
        uint64_t invalid: 1 = 0;

        bool operator<(const auto &b) const
        {
            int cmp = memcmp(hash.data(), b.hash.data(), hash.size());
            if (cmp != 0) return cmp < 0;
            return offset < b.offset;
        }

        bool index_less(const item &b) const
        {
            return memcmp(hash.data(), b.hash.data(), hash.size()) < 0;
        }

        bool operator==(const item &b) const
        {
            return memcmp(hash.data(), b.hash.data(), hash.size()) == 0;
        }
    };
    static_assert(sizeof(item) == 40);

    struct chunk_indexer: chunk_indexer_multi_part<item> {
        using chunk_indexer_multi_part<item>::chunk_indexer_multi_part;
    protected:
        void index_tx(const cardano::tx &tx) override
        {
            _idx.emplace_part(tx.hash().data()[0] / _part_range, tx.hash(), tx.offset(), 0);
        }

        void index_invalid_tx(const cardano::tx &tx) override
        {
            _idx.emplace_part(tx.hash().data()[0] / _part_range, tx.hash(), tx.offset(), 1);
        }
    };

    using indexer = indexer_offset<item, chunk_indexer>;
}

namespace fmt {
    template<>
    struct formatter<daedalus_turbo::index::tx::item>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "hash: {} offset: {}", v.hash, v.offset);
        }
    };
}

#endif //!DAEDALUS_TURBO_INDEX_TX_HPP