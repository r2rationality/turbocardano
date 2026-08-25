/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/types.hpp>

namespace turbo::cardano {
    pool_voting_thresholds_t pool_voting_thresholds_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return {
            decltype(motion_of_no_confidence)::from_cbor(it.read()),
            decltype(committee_normal)::from_cbor(it.read()),
            decltype(committee_no_confidence)::from_cbor(it.read()),
            decltype(hard_fork_initiation)::from_cbor(it.read()),
            decltype(security_voting_threshold)::from_cbor(it.read()),
        };
    }

    drep_voting_thresholds_t drep_voting_thresholds_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return {
            decltype(motion_no_confidence)::from_cbor(it.read()),
            decltype(committee_normal)::from_cbor(it.read()),
            decltype(committee_no_confidence)::from_cbor(it.read()),
            decltype(update_constitution)::from_cbor(it.read()),
            decltype(hard_fork_initiation)::from_cbor(it.read()),
            decltype(pp_network_group)::from_cbor(it.read()),
            decltype(pp_economic_group)::from_cbor(it.read()),
            decltype(pp_technical_group)::from_cbor(it.read()),
            decltype(pp_governance_group)::from_cbor(it.read()),
            decltype(treasury_withdrawal)::from_cbor(it.read()),
        };
    }
}
