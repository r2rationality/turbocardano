/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/types.hpp>

namespace turbo::cardano {
    void pool_voting_thresholds_t::to_cbor(era_encoder &enc) const
    {
        enc.array(5);
        motion_of_no_confidence.to_cbor(enc);
        committee_normal.to_cbor(enc);
        committee_no_confidence.to_cbor(enc);
        hard_fork_initiation.to_cbor(enc);
        security_voting_threshold.to_cbor(enc);
    }

    void drep_voting_thresholds_t::to_cbor(era_encoder &enc) const
    {
        enc.array(10);
        motion_no_confidence.to_cbor(enc);
        committee_normal.to_cbor(enc);
        committee_no_confidence.to_cbor(enc);
        update_constitution.to_cbor(enc);
        hard_fork_initiation.to_cbor(enc);
        pp_network_group.to_cbor(enc);
        pp_economic_group.to_cbor(enc);
        pp_technical_group.to_cbor(enc);
        pp_governance_group.to_cbor(enc);
        treasury_withdrawal.to_cbor(enc);
    }
}

