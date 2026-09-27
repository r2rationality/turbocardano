/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Certs.
// Included at class scope by lib/turbo/index/timed-update.hpp.

        bool operator<(const auto &b) const
        {
            if (loc.slot == b.loc.slot && loc.tx_idx == b.loc.tx_idx) {
                const auto phase = _phase(update);
                const auto other_phase = _phase(b.update);
                if (phase != other_phase)
                    return phase < other_phase;
                if (loc.cert_idx != b.loc.cert_idx)
                    return loc.cert_idx < b.loc.cert_idx;
                return update.index() < b.update.index();
            }
            return loc < b.loc;
        }

        static size_t _phase(const variant &v)
        {
            return std::visit<size_t>([](const auto &u) {
                using T = std::decay_t<decltype(u)>;
                // PV11 transaction-level effects, including withdrawals, run
                // before certificates. The legacy withdrawal remains a
                // separate update and must also precede certificates; Conway
                // ignores it at PV11 after applying the prelude instead.
                if constexpr (std::is_same_v<T, conway_tx_prelude>)
                    return 0;
                if constexpr (std::is_same_v<T, stake_withdraw>)
                    return 1;
                if constexpr (std::is_same_v<T, cardano::proposal_t>)
                    return 3;
                if constexpr (std::is_same_v<T, cardano::conway::vote_info_t>)
                    return 4;
                return 2;
            }, v);
        }
