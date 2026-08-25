#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <string>
#include <turbo/cardano/ledger/conway.hpp>
#include <turbo/cardano/ledger/conway/rule-result.hpp>

// Mirrors Ledger/Conway/Specification/Gov.lagda.md.
namespace turbo::cardano::ledger::conway::rules::gov {
    enum class failure {
        none,
        duplicate_action,
        deposit_mismatch,
        wrong_return_network,
        disallowed_during_bootstrap,
        unregistered_return_account,
        action_not_well_formed,
        action_invalid,
        missing_parent,
        invalid_hard_fork,
        unknown_action,
        expired_action,
        voter_not_allowed,
        no_active_committee,
        unknown_committee_voter,
        unknown_drep,
        unknown_pool,
        unsupported_voter_type
    };

    constexpr std::string_view failure_name(const failure f)
    {
        switch (f) {
            case failure::none: return "none";
            case failure::duplicate_action: return "duplicate action";
            case failure::deposit_mismatch: return "deposit mismatch";
            case failure::wrong_return_network: return "wrong return-account network";
            case failure::disallowed_during_bootstrap: return "action disallowed during bootstrap";
            case failure::unregistered_return_account: return "unregistered return account";
            case failure::action_not_well_formed: return "action is not well formed";
            case failure::action_invalid: return "action is invalid";
            case failure::missing_parent: return "missing or invalid parent";
            case failure::invalid_hard_fork: return "invalid hard-fork version";
            case failure::unknown_action: return "unknown action";
            case failure::expired_action: return "expired action";
            case failure::voter_not_allowed: return "voter is not allowed";
            case failure::no_active_committee: return "no active committee";
            case failure::unknown_committee_voter: return "unknown committee voter";
            case failure::unknown_drep: return "unknown DRep";
            case failure::unknown_pool: return "unknown pool";
            case failure::unsupported_voter_type: return "unsupported voter type";
        }
        return "unknown governance failure";
    }

    struct propose_environment {
        uint64_t current_epoch = 0;
        uint64_t expected_deposit = 0;
        uint8_t network_id = 0;
        const protocol_params &params;
        const enact_state_t &enact_state;
        const proposal_map &proposals;
        bool return_account_registered = false;
    };

    struct propose_effect {
        uint64_t proposed_in = 0;
        uint64_t expires_after = 0;
    };

    using propose_result = rule_result<propose_effect, failure>;

    enum class vote_target {
        committee,
        drep,
        pool
    };

    struct vote_environment {
        uint64_t current_epoch = 0;
        uint64_t committee_epoch = 0;
        const protocol_params &params;
        const enact_state_t &enact_state;
        const committee_t::member_key_map &committee_hot_keys;
        proposal_map &proposals;
        drep_info_map &dreps;
        bool pool_registered = false;
    };

    struct vote_effect {
        gov_action_state_t *action = nullptr;
        vote_target target = vote_target::committee;
    };

    using vote_result = rule_result<vote_effect, failure>;

    bool same_parent_group(const gov_action_t &, const gov_action_t &);
    bool is_bootstrap_action(const gov_action_t &);
    bool action_has_parent(const gov_action_t &);
    optional_gov_action_id_t action_parent(const gov_action_t &);
    bool prev_action_as_expected(const gov_action_t &, const enact_state_t &);
    std::string action_label(const gov_action_t &);
    bool action_well_formed(const gov_action_t &, const protocol_version &, uint8_t network_id);
    bool action_valid(const gov_action_t &, const enact_state_t &, uint64_t current_epoch);
    bool has_valid_parent(const gov_action_t &, const enact_state_t &, const proposal_map &);
    bool valid_hard_fork(const gov_action_t &, const enact_state_t &, const proposal_map &);
    bool can_vote(const gov_action_t &, voter_t::type_t, const protocol_version &);
    bool committee_member_active(const committee_t::member_key_map &, const credential_t &, uint64_t expire_epoch, uint64_t current_epoch);
    propose_result propose(const propose_environment &, const proposal_t &);
    vote_result vote(const vote_environment &, const vote_info_t &);
}
