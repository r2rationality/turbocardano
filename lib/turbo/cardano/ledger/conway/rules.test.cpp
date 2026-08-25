/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/ledger/conway/rules.hpp>
#include <turbo/common/test.hpp>

namespace {
    using namespace turbo;
    using namespace cardano;
    using namespace ledger::conway;
}

suite cardano_ledger_conway_rules_suite = [] {
    "Conway formal rule IDs"_test = [] {
        expect(rules::name(rules::rule_id::gov_propose) == "GOV-Propose");
        expect(rules::name(rules::rule_id::gov_vote) == "GOV-Vote");
        expect(rules::name(rules::rule_id::ratify_accept) == "RATIFY-Accept");
        expect(rules::name(rules::rule_id::enact_wdrl) == "Enact-Wdrl");
        const cert_t certificate { reg_drep_cert {} };
        expect(rules::certs::transition_rule(certificate) == rules::rule_id::cert_vdel);
        expect(rules::certs::constructor_rule(certificate) == rules::rule_id::govcert_regdrep);
    };

    "GOV-Propose"_test = [] {
        protocol_params params {};
        enact_state_t enact_state {};
        proposal_map proposals {};
        proposal_t proposal {};
        proposal.procedure.return_addr_network_id = 1;
        const rules::gov::propose_environment env {
            3,
            proposal.procedure.deposit,
            1,
            params,
            enact_state,
            proposals,
            true
        };
        const auto accepted = rules::gov::propose(env, proposal);
        expect(static_cast<bool>(accepted));
        expect(accepted.rule == rules::rule_id::gov_propose);
        proposals.try_emplace(proposal.id);
        const auto duplicate = rules::gov::propose(env, proposal);
        expect(!static_cast<bool>(duplicate));
        expect(duplicate.failure == rules::gov::failure::duplicate_action);
    };

    "GOV-Vote restricts unelected committee members from protocol 11"_test = [] {
        protocol_params params {};
        enact_state_t enact_state {};
        enact_state.committee.emplace();
        proposal_map proposals {};
        const gov_action_id_t action_id {
            crypto::blake2b::digest<tx_hash>(std::string_view { "action" }),
            0
        };
        auto &action = proposals.try_emplace(action_id).first->second;
        action.proposal.action.val = gov_action_t::info_action_t {};
        action.expires_after = 10;
        const credential_t cold_id {
            crypto::blake2b::digest<key_hash>(std::string_view { "cold" }),
            false
        };
        const credential_t hot_id {
            crypto::blake2b::digest<key_hash>(std::string_view { "hot" }),
            false
        };
        committee_t::member_key_map hot_keys {};
        hot_keys[cold_id].val = hot_id;
        drep_info_map dreps {};
        const vote_info_t procedure {
            voter_t { voter_t::const_comm_key, hot_id.hash },
            action_id,
            voting_procedure_t { vote_t::yes }
        };

        params.protocol_ver = { 10, 0 };
        rules::gov::vote_environment env {
            1, 1, params, enact_state, hot_keys, proposals, dreps, false
        };
        expect(static_cast<bool>(rules::gov::vote(env, procedure)));

        params.protocol_ver = { 11, 0 };
        const auto rejected = rules::gov::vote(env, procedure);
        expect(!static_cast<bool>(rejected));
        expect(rejected.failure == rules::gov::failure::unknown_committee_voter);
    };

    "RATIFY-Accept"_test = [] {
        gov_action_state_t action {};
        action.proposal.action.val = gov_action_t::no_confidence_t {};
        action.expires_after = 10;
        const rules::ratify::environment env {
            3,
            true,
            true,
            false,
            true,
            true,
            true,
            true
        };
        const auto decision = rules::ratify::step(env, action);
        expect(decision.rule == rules::rule_id::ratify_accept);
        expect(decision.outcome == rules::ratify::decision::kind::accept);
    };

    "RATIFY-Reject"_test = [] {
        gov_action_state_t action {};
        action.expires_after = 2;
        const rules::ratify::environment env { 3 };
        const auto decision = rules::ratify::step(env, action);
        expect(decision.rule == rules::rule_id::ratify_reject);
        expect(decision.outcome == rules::ratify::decision::kind::reject);
    };

    "RATIFY-Continue"_test = [] {
        gov_action_state_t action {};
        action.expires_after = 3;
        const rules::ratify::environment env { 3 };
        const auto decision = rules::ratify::step(env, action);
        expect(decision.rule == rules::rule_id::ratify_continue);
        expect(decision.outcome == rules::ratify::decision::kind::continue_);
    };

    "Enact-NoConf"_test = [] {
        enact_state_t state {};
        state.committee.emplace();
        const auto result = rules::enact::no_confidence(
            state,
            gov_action_id_t {},
            gov_action_t::no_confidence_t {});
        expect(static_cast<bool>(result));
        expect(result.rule == rules::rule_id::enact_no_conf);
        expect(!state.committee.has_value());
    };

    "Enact-UpdComm rejects an excessive term"_test = [] {
        enact_state_t state {};
        state.params.committee_max_term_length = 2;
        gov_action_t::update_committee_t action {};
        action.members_to_add.try_emplace(credential_t {}, 4);
        const auto result = rules::enact::update_committee(
            state,
            gov_action_id_t {},
            action,
            1);
        expect(!static_cast<bool>(result));
        expect(result.failure == rules::enact::failure::committee_term_too_long);
    };

    "Enact-UpdComm"_test = [] {
        enact_state_t state {};
        state.params.committee_max_term_length = 2;
        gov_action_t::update_committee_t action {};
        action.members_to_add.try_emplace(credential_t {}, 3);
        const auto result = rules::enact::update_committee(
            state,
            gov_action_id_t {},
            action,
            1);
        expect(static_cast<bool>(result));
        expect(result.rule == rules::rule_id::enact_upd_comm);
    };

    "Enact-NewConst"_test = [] {
        enact_state_t state {};
        const auto result = rules::enact::new_constitution(
            state,
            gov_action_id_t {},
            gov_action_t::new_constitution_t {});
        expect(static_cast<bool>(result));
        expect(result.rule == rules::rule_id::enact_new_const);
    };

    "Enact-HF"_test = [] {
        enact_state_t state {};
        gov_action_t::hard_fork_init_t action {};
        action.protocol_ver = { 10, 0 };
        const auto result = rules::enact::hard_fork(
            state,
            gov_action_id_t {},
            action);
        expect(static_cast<bool>(result));
        expect(result.rule == rules::rule_id::enact_hf);
    };

    "Enact-PParams"_test = [] {
        enact_state_t state {};
        const auto result = rules::enact::parameter_change(
            state,
            gov_action_id_t {},
            gov_action_t::parameter_change_t {});
        expect(static_cast<bool>(result));
        expect(result.rule == rules::rule_id::enact_pparams);
    };

    "Enact-Wdrl rejects insufficient treasury"_test = [] {
        enact_state_t state {};
        gov_action_t::treasury_withdrawals_t action {};
        action.withdrawals.try_emplace(reward_id_t {}, 1);
        const auto result = rules::enact::treasury_withdrawal(
            state,
            gov_action_id_t {},
            action);
        expect(!static_cast<bool>(result));
        expect(result.rule == rules::rule_id::enact_wdrl);
        expect(result.failure == rules::enact::failure::treasury_insufficient);
    };

    "Enact-Info"_test = [] {
        enact_state_t state {};
        const auto result = rules::enact::info(
            state,
            gov_action_id_t {},
            gov_action_t::info_action_t {});
        expect(static_cast<bool>(result));
        expect(result.rule == rules::rule_id::enact_info);
    };
};
