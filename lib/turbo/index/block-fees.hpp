/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */
#ifndef DAEDALUS_TURBO_INDEX_BLOCK_FEES_HPP
#define DAEDALUS_TURBO_INDEX_BLOCK_FEES_HPP

#include <turbo/cardano/common/types.hpp>
#include <turbo/cardano/conway/block.hpp>
#include <turbo/index/common.hpp>

namespace turbo::index::block_fees {
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
#include <turbo/cardano/ledger/rules/block-body/index.ipp>
    };
    using indexer = indexer_one_epoch<chunk_indexer>;
}

#endif //!DAEDALUS_TURBO_INDEX_BLOCK_FEES_HPP
