/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Ratify.
// Included at class scope by lib/turbo/cardano/ledger/conway.hpp.

        size_t active_size(const member_key_map &hot_keys, const uint64_t current_epoch) const
        {
            size_t size = 0;
            for (const auto &[cold_id, expire_epoch]: members) {
                const auto hot_it = hot_keys.find(cold_id);
                if (current_epoch <= expire_epoch
                        && hot_it != hot_keys.end()
                        && std::holds_alternative<credential_t>(hot_it->second.val)) {
                    ++size;
                }
            }
            return size;
        }
