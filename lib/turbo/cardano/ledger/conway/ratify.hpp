#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/ledger/conway.hpp>
#include <turbo/cardano/ledger/conway/rule-id.hpp>

// Mirrors Ledger/Conway/Specification/Ratify.lagda.md.
namespace turbo::cardano::ledger::conway::rules::ratify {
    struct environment {
        uint64_t current_epoch = 0;
        bool previous_action_matches = false;
        bool committee_term_valid = false;
        bool delayed = false;
        bool treasury_sufficient = false;
        bool accepted_by_committee = false;
        bool accepted_by_pools = false;
        bool accepted_by_dreps = false;
    };

    struct decision {
        enum class kind {
            accept,
            reject,
            continue_
        };

        rule_id rule = rule_id::ratify_continue;
        kind outcome = kind::continue_;
        bool delays_following_actions = false;
    };

    bool valid_committee_term(const gov_action_t &, const protocol_params &, uint64_t current_epoch);
    bool withdrawals_can_withdraw(const gov_action_t &, const enact_state_t &);
    decision accept(bool delays_following_actions);
    decision reject();
    decision continue_();
    decision step(const environment &, const gov_action_state_t &);
}
