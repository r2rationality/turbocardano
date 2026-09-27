/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/ledger/rules/certs/conway.hpp>
#include <turbo/cardano/ledger/conway/detail.hpp>

#include <turbo/cardano/ledger/rules/govcert/premises.ipp>

namespace turbo::cardano::ledger::conway::rules::certs {
    rule_id transition_rule(const cert_t &certificate)
    {
        return std::visit([](const auto &signal) {
            using T = std::decay_t<decltype(signal)>;
            if constexpr (std::is_same_v<T, pool_reg_cert>
                    || std::is_same_v<T, pool_retire_cert>) {
                return rule_id::cert_pool;
            } else if constexpr (std::is_same_v<T, genesis_deleg_cert>
                    || std::is_same_v<T, instant_reward_cert>) {
                return rule_id::unsupported_certificate;
            } else if constexpr (std::is_same_v<T, auth_committee_hot_cert>
                    || std::is_same_v<T, resign_committee_cold_cert>
                    || std::is_same_v<T, reg_drep_cert>
                    || std::is_same_v<T, unreg_drep_cert>
                    || std::is_same_v<T, update_drep_cert>) {
                return rule_id::cert_vdel;
            } else {
                return rule_id::cert_deleg;
            }
        }, certificate.val);
    }

    rule_id constructor_rule(const cert_t &certificate)
    {
        return std::visit([](const auto &signal) {
            using T = std::decay_t<decltype(signal)>;
            if constexpr (std::is_same_v<T, pool_reg_cert>) {
                return rule_id::pool_regpool;
            } else if constexpr (std::is_same_v<T, pool_retire_cert>) {
                return rule_id::pool_retirepool;
            } else if constexpr (std::is_same_v<T, auth_committee_hot_cert>
                    || std::is_same_v<T, resign_committee_cold_cert>) {
                return rule_id::govcert_ccreghot;
            } else if constexpr (std::is_same_v<T, reg_drep_cert>
                    || std::is_same_v<T, update_drep_cert>) {
                return rule_id::govcert_regdrep;
            } else if constexpr (std::is_same_v<T, unreg_drep_cert>) {
                return rule_id::govcert_deregdrep;
            } else if constexpr (std::is_same_v<T, reg_cert>
                    || std::is_same_v<T, stake_reg_cert>) {
                return rule_id::deleg_reg;
            } else if constexpr (std::is_same_v<T, unreg_cert>
                    || std::is_same_v<T, stake_dereg_cert>) {
                return rule_id::deleg_dereg;
            } else if constexpr (std::is_same_v<T, stake_deleg_cert>
                    || std::is_same_v<T, vote_deleg_cert>
                    || std::is_same_v<T, stake_vote_deleg_cert>
                    || std::is_same_v<T, stake_reg_deleg_cert>
                    || std::is_same_v<T, vote_reg_deleg_cert>
                    || std::is_same_v<T, stake_vote_reg_deleg_cert>) {
                return rule_id::deleg_delegate;
            } else {
                return rule_id::unsupported_certificate;
            }
        }, certificate.val);
    }
}

namespace turbo::cardano::ledger::conway {
#include <turbo/cardano/ledger/rules/deleg/conway.ipp>

#include <turbo/cardano/ledger/rules/govcert/state.ipp>

}
