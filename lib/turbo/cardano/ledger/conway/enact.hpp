#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/ledger/conway.hpp>
#include <turbo/cardano/ledger/conway/rule-result.hpp>

// Mirrors Ledger/Conway/Specification/Enact.lagda.md.
namespace turbo::cardano::ledger::conway::rules::enact {
    enum class failure {
        none,
        committee_term_too_long,
        treasury_insufficient
    };

    using result = validation_result<failure>;

    result no_confidence(enact_state_t &, const gov_action_id_t &, const gov_action_t::no_confidence_t &);
    result update_committee(enact_state_t &, const gov_action_id_t &, const gov_action_t::update_committee_t &, uint64_t current_epoch);
    result new_constitution(enact_state_t &, const gov_action_id_t &, const gov_action_t::new_constitution_t &);
    result hard_fork(enact_state_t &, const gov_action_id_t &, const gov_action_t::hard_fork_init_t &);
    result parameter_change(enact_state_t &, const gov_action_id_t &, const gov_action_t::parameter_change_t &);
    result treasury_withdrawal(enact_state_t &, const gov_action_id_t &, const gov_action_t::treasury_withdrawals_t &);
    result info(enact_state_t &, const gov_action_id_t &, const gov_action_t::info_action_t &);
    result step(enact_state_t &, const gov_action_id_t &, const gov_action_t &, uint64_t current_epoch);
}
