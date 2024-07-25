/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2024 Alex Sierkov (alex dot sierkov at gmail dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */
#ifndef DAEDALUS_TURBO_INDEX_BLOCK_FEES_HPP
#define DAEDALUS_TURBO_INDEX_BLOCK_FEES_HPP

#include <dt/cardano/type.hpp>
#include <dt/index/common.hpp>

namespace daedalus_turbo::index::block_fees {
    struct item {
        uint64_t slot = 0;
        cardano::pool_hash issuer_id {};
        uint64_t fees = 0;
        uint64_t end_offset = 0;
        uint8_t era = 0;

        bool operator<(const auto &b) const
        {
            if (slot != b.slot)
                return slot < b.slot;
            return memcmp(issuer_id.data(), b.issuer_id.data(), issuer_id.size()) < 0;
        }
    };

    struct chunk_indexer: chunk_indexer_one_epoch<item> {
        using chunk_indexer_one_epoch::chunk_indexer_one_epoch;
    protected:
        void _index_epoch(const cardano::block_base &blk, data_type &idx) override
        {
            uint64_t fees = 0;
            blk.foreach_tx([&](const auto &tx) {
                fees += tx.fee();
            });
            idx.emplace_back(blk.slot(), blk.issuer_hash(), fees, blk.offset() + blk.size(), static_cast<uint8_t>(blk.era()));
        }
    };
    using indexer = indexer_one_epoch<chunk_indexer>;
}

#endif //!DAEDALUS_TURBO_INDEX_BLOCK_FEES_HPP