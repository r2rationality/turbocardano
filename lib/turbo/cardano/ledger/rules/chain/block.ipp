/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Chain.
// Included at class scope by lib/turbo/validator.cpp.

        void my_on_block_validate(const cardano::block_base &blk) const
        {
            auto slot = blk.slot();
            if (!blk.signature_ok()) [[unlikely]]
                throw error(fmt::format("validation of the block signature at slot {} failed!", slot));
            if (blk.era() > 0 && !blk.body_hash_ok()) [[unlikely]]
                throw error(fmt::format("validation of the block body hash at slot {} failed!", slot));
            switch (blk.era()) {
                case 0: {
                    static auto boundary_issuer_vkey = cardano::vkey::from_hex("0000000000000000000000000000000000000000000000000000000000000000");
                    if (blk.issuer_vkey() != boundary_issuer_vkey) [[unlikely]]
                        throw error(fmt::format("boundary block contains an unexpected issuer_vkey: {}", blk.issuer_vkey()));
                    break;
                }
                case 1: {
                    break;
                }
                case 2:
                case 3:
                case 4:
                case 5:
                case 6:
                case 7:
                    // do nothing here, the block signer's eligibility is tested later process_vrf_chunks
                    break;
                [[unlikely]] default:
                    throw error(fmt::format("unsupported block era: {}", blk.era()));
            }
        }
