/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Utxo.
// Included at namespace scope by lib/turbo/cardano/ledger/shelley.cpp.

    static void _update_stake_delta(stake_update_map &deltas, const cardano::stake_ident &stake_id, const int64_t delta)
    {
        if (delta) {
            const auto [it, created] = deltas.try_emplace(stake_id, delta);
            if (!created) {
                it->second += delta;
                if (!it->second)
                    deltas.erase(it);
            }
        }
    }
