/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/cert.hpp>

namespace turbo::cardano {
    std::optional<credential_t> cert_t::signing_cred() const
    {
        std::optional<credential_t> cred {};
        std::visit([&](const auto &c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, auth_committee_hot_cert>
                    || std::is_same_v<T, resign_committee_cold_cert>) {
                cred.emplace(c.cold_id);
            } else if constexpr (std::is_same_v<T, reg_drep_cert>
                   || std::is_same_v<T, unreg_drep_cert>
                   || std::is_same_v<T, update_drep_cert>) {
                cred.emplace(c.drep_id);
            } else if constexpr (std::is_same_v<T, pool_reg_cert>
                    || std::is_same_v<T, pool_retire_cert>) {
                cred.emplace(c.pool_id, false);
            } else if constexpr (std::is_same_v<T, genesis_deleg_cert>) {
                cred.emplace(c.hash, false);
            } else if constexpr (std::is_same_v<T, stake_reg_cert>) {
                // nothing - stake registration does not require certification
            } else if constexpr (std::is_same_v<T, instant_reward_cert>) {
                // nothing here - a quorum of genesis signers is checked in a different way
            } else {
                cred.emplace(c.stake_id);
            }
        }, val);
        return cred;
    }
}
