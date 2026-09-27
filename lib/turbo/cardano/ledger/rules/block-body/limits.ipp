/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.BlockBody.
// Included at class scope by lib/turbo/txwit/validator.cpp.

            void _validate_max_stats(const batch_info &part) const
            {
                const auto &max = part.max_stats;
                if (max.max_block_body_size && *max.max_block_body_size > _st.params().max_block_body_size) [[unlikely]]
                    throw error(fmt::format("block body size of {} exceeded the limit of {}", *max.max_block_body_size, _st.params().max_block_body_size));
                if (max.max_block_header_size && *max.max_block_header_size > _st.params().max_block_header_size) [[unlikely]]
                    throw error(fmt::format("block header size of {} exceeded the limit of {}", *max.max_block_header_size, _st.params().max_block_header_size));
                if (max.max_tx_size && *max.max_tx_size > _st.params().max_transaction_size) [[unlikely]]
                    throw error(fmt::format("tx size of {} exceeded the limit of {}", *max.max_tx_size, _st.params().max_transaction_size));
            }
