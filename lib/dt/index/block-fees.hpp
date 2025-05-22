/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */
#ifndef DAEDALUS_TURBO_INDEX_BLOCK_FEES_HPP
#define DAEDALUS_TURBO_INDEX_BLOCK_FEES_HPP

#include <dt/cardano/common/types.hpp>
#include <dt/cardano/conway/block.hpp>
#include <dt/index/common.hpp>

namespace daedalus_turbo::index::block_fees {
    struct item {
        uint64_t slot = 0;
        cardano::pool_hash issuer_id {};
        uint64_t fees = 0;
        uint64_t donations = 0;
        uint64_t end_offset = 0;
        uint8_t era = 0;

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.slot, self.issuer_id, self.fees, self.donations, self.end_offset, self.era);
        }

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
        void _index_epoch(const cardano::block_container &blk, data_type &idx) override
        {
            uint64_t fees = 0;
            uint64_t donations = 0;
            blk->foreach_tx([&](const auto &tx) {
                if (blk->era() > 1) // byron era validation does not require access to fees, which itself are harder to compute
                    fees += tx.fee();
                if (const auto *c_tx = dynamic_cast<const cardano::conway::tx *>(&tx); c_tx) {
                    if (const auto d = c_tx->donation(); d)
                        donations += d;
                }
            });
            idx.emplace_back(blk->slot(), blk->issuer_hash(), fees, donations, blk.offset() + blk.size(), numeric_cast<uint8_t>(blk->era()));
        }
    };
    using indexer = indexer_one_epoch<chunk_indexer>;
}

#endif //!DAEDALUS_TURBO_INDEX_BLOCK_FEES_HPP