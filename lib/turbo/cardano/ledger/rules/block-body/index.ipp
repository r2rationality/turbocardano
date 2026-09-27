/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.BlockBody.
// Included at class scope by lib/turbo/index/block-fees.hpp.

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
