/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/ledger/rules/conway.hpp>
#include <turbo/cardano/ledger/updates.hpp>
#include <turbo/common/test.hpp>

namespace {
    using namespace turbo;
    using namespace cardano;
    using namespace ledger::conway;

    struct gap_test_state: state {
        using state::_begin_transaction;
        using state::_process_timed_updates;
        using state::_certificate_tx;
        using state::_proposal_valid;
        using state::_register_delegating_stake;
        using state::register_stake;
        using state::retire_stake;
        using state::_accounts;
        using state::_committee_hot_keys;
        using state::_enact_state;
        using state::_ratify_state;
        using state::_ratify_ready;
        using state::_epoch;
        using state::_treasury;
        using state::_gov_make_pulsing_snapshot;
        using state::_params;
        using state::_proposals;
        using state::_deposited;
        using state::_drep_state;
        using state::_mark;
        using state::_pulsing_data;
        using state::_pool_default_vote;
        using state::_snapshot_pool_default_votes;
        using state::_compute_drep_voting_power;
        using state::_compute_pool_voting_power;
        using state::delegate_vote;
        using state::delegate_stake;

        gap_test_state()
        {
            _params.protocol_ver = { 11, 0 };
            _params.key_deposit = 10;
        }

        proposal_t proposal_for(const stake_ident &id) const
        {
            proposal_t p {};
            p.procedure.deposit = _params.gov_action_deposit;
            p.procedure.return_addr = id;
            p.procedure.return_addr_network_id = _cfg.shelley_network_id;
            p.procedure.action.val = gov_action_t::info_action_t {};
            return p;
        }

        void seed(const stake_ident &id, const uint64_t reward=0)
        {
            auto &acc = _accounts[id];
            acc.ptr.emplace(0, 0, 0);
            acc.deposit = 10;
            acc.reward = reward;
            _deposited += 10;
        }
    };

    reward_id_t gap_reward(const uint8_t suffix)
    {
        reward_id_t id {};
        id[0] = 0xE1;
        id[28] = suffix;
        return id;
    }
}

suite cardano_ledger_conway_rules_suite = [] {
    "LEDGERS allows same-transaction registration of governance return accounts"_test = [] {
        // Return credential of mainnet info proposal BD488931...#0, epoch 556:
        // certificate tag 7 registers this script credential in the same transaction.
        const stake_ident id { script_hash::from_hex("58e8e98c65b608a9a9ebdea42f07ddb4ca898aefaa10f4d0145b995e"), true };
        const auto updates = [&](gap_test_state &st, uint32_t proposal_tx) {
            ledger::timed_update_list result {};
            auto &reg = result.emplace_back();
            reg.loc = { 0, 1, 0 };
            reg.update = reg_cert { id, 10 };
            auto &proposal = result.emplace_back();
            proposal.loc = { 0, proposal_tx, 1 };
            proposal.update = st.proposal_for(id);
            return result;
        };
        for (const auto major: { 10U, 11U }) {
            gap_test_state same {};
            same._params.protocol_ver.major = major;
            expect(nothrow([&] { same._process_timed_updates(updates(same, 1)); }));
            expect(same._proposals.size() == 1_u);
            expect(!same._certificate_tx.has_value());
            gap_test_state next {};
            next._params.protocol_ver.major = major;
            expect(nothrow([&] { next._process_timed_updates(updates(next, 2)); }));
            expect(next._proposals.size() == 1_u);
            expect(!next._certificate_tx.has_value());

            gap_test_state retired {};
            retired._params.protocol_ver.major = major;
            retired.seed(id);
            auto unregistration = updates(retired, 1);
            unregistration.front().update = unreg_cert { id, 10 };
            expect(throws([&] { retired._process_timed_updates(std::move(unregistration)); }));
            expect(retired._proposals.empty());
        }
    };

    "CERTS defers DRep cleanup until the final registration domain"_test = [] {
        gap_test_state st {};
        const stake_ident account = gap_reward(1), drep = gap_reward(2);
        const cert_loc_t first { 0, 1, 0 }, next { 0, 2, 0 };
        st.seed(account);
        st.process_cert(reg_drep_cert { drep, st._params.drep_deposit, {} }, first);
        st.delegate_vote(account, drep_t { drep }, first);
        st._proposals[gov_action_id_t {}].drep_votes[drep] = voting_procedure_t { vote_t::yes };
        st.process_cert(unreg_drep_cert { drep, st._params.drep_deposit }, first);
        st.process_cert(reg_drep_cert { drep, st._params.drep_deposit, {} }, first);
        st.finish_certificates();
        expect(st._accounts.at(account).vote_deleg.has_value());
        expect(st._drep_state.at(drep).delegs.contains(account));
        expect(st._proposals.at(gov_action_id_t {}).drep_votes.contains(drep));
        st.process_cert(unreg_drep_cert { drep, st._params.drep_deposit }, next);
        st._begin_transaction({ 0, 3, 0 }); // the next transaction finalizes CERTS
        expect(!st._accounts.at(account).vote_deleg.has_value());
        expect(!st._proposals.at(gov_action_id_t {}).drep_votes.contains(drep));
    };

    "CERTS respects redelegation between DRep deregistration and registration"_test = [] {
        gap_test_state st {};
        const stake_ident account = gap_reward(1), a = gap_reward(2), b = gap_reward(3);
        const cert_loc_t loc { 0, 1, 0 };
        st.seed(account);
        st.process_cert(reg_drep_cert { a, st._params.drep_deposit, {} }, loc);
        st.process_cert(reg_drep_cert { b, st._params.drep_deposit, {} }, loc);
        st.delegate_vote(account, drep_t { a }, loc);
        st.process_cert(unreg_drep_cert { a, st._params.drep_deposit }, loc);
        st.delegate_vote(account, drep_t { b }, loc);
        st.process_cert(reg_drep_cert { a, st._params.drep_deposit, {} }, loc);
        st.finish_certificates();
        expect(!st._drep_state.at(a).delegs.contains(account));
        expect(st._drep_state.at(b).delegs.contains(account));
        expect(std::get<credential_t>(st._accounts.at(account).vote_deleg->val) == b);
    };

    "PRE-CERT and DELEG reject cached unregistered accounts"_test = [] {
        gap_test_state st {};
        const stake_ident id = gap_reward(1);
        st._accounts[id].stake = 100;
        expect(throws([&] { st.withdraw_reward(id, 0); }));
        expect(throws([&] { st.delegate_vote(id, drep_t {}, {}); }));
        expect(throws([&] { st.delegate_stake(id, pool_hash {}); }));
        expect(!st._accounts.at(id).vote_deleg.has_value());
        st.seed(id);
        expect(throws([&] { st.withdraw_reward(id, 0); }));
        st.delegate_vote(id, drep_t {}, {});
        st.withdraw_reward(id, 0);
        expect(st._accounts.at(id).vote_deleg.has_value());
    };

    "withdrawal vote delegation is required from PV10 in batched updates"_test = [] {
        const auto reward = gap_reward(1);
        const stake_ident id = reward;
        const auto updates = [](const reward_id_t &reward_id) {
            ledger::timed_update_list result {};
            turbo::index::timed_update::conway_tx_prelude prelude {};
            prelude.withdrawals.push_back({ reward_id, 25 });
            auto &first = result.emplace_back();
            first.loc = { 0, 1, 0 };
            first.update = std::move(prelude);
            // Indexing emits both forms; PV9/10 use the legacy update, PV11 the prelude.
            auto &legacy = result.emplace_back();
            legacy.loc = { 0, 1, 0 };
            legacy.update = turbo::index::timed_update::stake_withdraw { static_cast<stake_ident>(reward_id), 25 };
            return result;
        };
        for (const auto major: { 9U, 10U, 11U }) {
            gap_test_state st {};
            st._params.protocol_ver.major = major;
            st.seed(id, 25);
            if (major == 9) {
                expect(nothrow([&] { st._process_timed_updates(updates(reward)); }));
                expect(st._accounts.at(id).reward == 0_u);
            } else {
                expect(throws([&] { st._process_timed_updates(updates(reward)); }));
                expect(st._accounts.at(id).reward == 25_u);
            }

            gap_test_state delegated {};
            delegated._params.protocol_ver.major = major;
            delegated.seed(id, 25);
            delegated.delegate_vote(id, drep_t {}, {});
            expect(nothrow([&] { delegated._process_timed_updates(updates(reward)); }));
            expect(delegated._accounts.at(id).reward == 0_u);

            auto script_reward = reward;
            script_reward[0] = 0xF1;
            const stake_ident script_id = script_reward;
            gap_test_state script {};
            script._params.protocol_ver.major = major;
            script.seed(script_id, 25);
            expect(nothrow([&] { script._process_timed_updates(updates(script_reward)); }));
            expect(script._accounts.at(script_id).reward == 0_u);
        }
    };

    "PV9 withdrawal exemption retains registration and full balance checks"_test = [] {
        gap_test_state st {};
        st._params.protocol_ver = { 9, 0 };
        const stake_ident id = gap_reward(1);
        st._accounts[id].reward = 25;
        expect(throws([&] { st.withdraw_reward(id, 25); }));
        st.seed(id, 25);
        expect(throws([&] { st.withdraw_reward(id, 24); }));
        expect(st._accounts.at(id).reward == 25_u);
        expect(nothrow([&] { st.withdraw_reward(id, 25); }));
        expect(st._accounts.at(id).reward == 0_u);
    };

    "RATIFY freezes pool defaults across subsequent delegation changes"_test = [] {
        gap_test_state st {};
        const stake_ident id = gap_reward(1);
        pool_params params {};
        params.reward_id = gap_reward(1);
        st._mark.pool_params.try_emplace(pool_hash {}, params);
        st.seed(id);
        st._accounts.at(id).vote_deleg = drep_t {};
        st._snapshot_pool_default_votes();
        st._accounts.at(id).vote_deleg = drep_t { drep_t::no_confidence_t {} };
        expect(st._pool_default_vote(pool_hash {}) == pool_default_vote_t::abstain);
        st._snapshot_pool_default_votes();
        expect(st._pool_default_vote(pool_hash {}) == pool_default_vote_t::no_confidence);
    };

    "RATIFY stake uses pre-payout rewards and mark pool delegations"_test = [] {
        gap_test_state st {};
        const stake_ident id = gap_reward(1);
        st.seed(id);
        auto &acc = st._accounts.at(id);
        acc.stake = 100;
        acc.mark_stake = 100;
        acc.reward = 50; // boundary payout not in the input reward map
        acc.vote_deleg = drep_t {};
        expect(st._compute_drep_voting_power().at(drep_t {}) == 100_u);
        acc.mark_deleg = pool_hash {};
        acc.deleg.reset(); // retired by POOLREAP after mark was captured
        st._mark.pool_params.try_emplace(pool_hash {}, pool_params {});
        auto &proposal = st._proposals[gov_action_id_t {}];
        proposal.proposal.return_addr = id;
        proposal.proposal.deposit = 7;
        expect(st._compute_pool_voting_power().get(pool_hash {}) == 7_u);
    };

    "EPOCH expired retained proposals do not start dormancy"_test = [] {
        gap_test_state st {};
        st._proposals[gov_action_id_t {}].expires_after = 0;
        st.start_epoch(1);
        expect(st.num_dormant_epochs() == 0_u);
        st._proposals.clear();
        st.start_epoch(2);
        expect(st.num_dormant_epochs() == 1_u);
    };

    "Conway frozen inputs survive pulser serialization"_test = [] {
        const file::tmp path { "conway-pool-default-votes.zpp" };
        pulsing_data_t original {};
        original.pool_default_votes.emplace(pool_hash {}, pool_default_vote_t::abstain);
        original.treasury = 123;
        ledger::zpp_encoder enc {};
        original.to_zpp(enc);
        enc.run(scheduler::get(), "encode-pool-vote-test");
        enc.save(path.path(), true);
        pulsing_data_t restored {};
        ledger::parallel_decoder dec { path.path() };
        restored.from_zpp(dec);
        dec.run(scheduler::get(), "decode-pool-vote-test");
        expect(restored.pool_default_votes.at(pool_hash {}) == pool_default_vote_t::abstain);
        expect_equal(restored.treasury, original.treasury);
    };

    "early governance completion is retained without applying proposals"_test = [] {
        gap_test_state st {};
        st._epoch = 2;
        st._treasury = 100;
        const gov_action_id_t expired_id {};
        gov_action_state_t expired {};
        expired.proposal.action.val = gov_action_t::info_action_t {};
        expired.proposed_in = 0;
        expired.expires_after = 1;
        st._proposals.emplace(expired_id, expired);
        st._gov_make_pulsing_snapshot();
        expect_equal(st._pulsing_data.treasury, 100);
        st.complete_pulsers();
        expect(st._ratify_ready);
        expect(st._ratify_state.expired.contains(expired_id));
        expect(st._proposals.contains(expired_id));
        expect_equal(st._treasury, 100);

        const file::tmp path { "conway-completed-pulser.zpp" };
        ledger::zpp_encoder enc {};
        st.to_zpp(enc);
        enc.run(scheduler::get(), "encode-completed-governance");
        enc.save(path.path(), true);
        gap_test_state restored {};
        ledger::parallel_decoder dec { path.path() };
        restored.from_zpp(dec);
        dec.run(scheduler::get(), "decode-completed-governance");
        expect(restored._ratify_ready);
        expect(restored._ratify_state.expired.contains(expired_id));
        restored._pulsing_data.proposals.clear();
        restored.complete_pulsers();
        expect(restored._ratify_state.expired.contains(expired_id));
        expect(restored._proposals.contains(expired_id));
    };

    "ratification uses frozen treasury regardless of completion time"_test = [] {
        for (const uint64_t live_treasury: { 0, 1'000 }) {
            gap_test_state st {};
            st._epoch = 2;
            st._treasury = 100;
            st._enact_state.params.protocol_ver = { 11, 0 };
            st._enact_state.params.committee_min_size = 0;
            st._enact_state.params.drep_voting_thresholds.treasury_withdrawal = { 0, 1 };
            st._enact_state.committee = committee_t { {}, { 0, 1 } };
            st._ratify_state.new_state = st._enact_state;
            gov_action_state_t action {};
            action.proposed_in = 1;
            action.expires_after = 10;
            gov_action_t::treasury_withdrawals_t withdrawals {};
            withdrawals.withdrawals.emplace(gap_reward(1), 75);
            action.proposal.action.val = withdrawals;
            const gov_action_id_t gid {};
            st._proposals.emplace(gid, action);
            st._gov_make_pulsing_snapshot();
            st._treasury = live_treasury;
            st.complete_pulsers();
            expect(st._ratify_ready);
            expect_equal(st._ratify_state.enacted.size(), 1);
            expect_equal(st._ratify_state.new_state.withdrawals.at(static_cast<stake_ident>(gap_reward(1))), 75);
            expect_equal(st._treasury, live_treasury);
            st.complete_pulsers();
            expect_equal(st._ratify_state.enacted.size(), 1);
        }
    };

    "RATIFY exempts committee replacement without a committee"_test = [] {
        gap_test_state st {};
        st._ratify_state.new_state.committee.reset();
        gov_action_state_t action {};
        action.proposal.action.val = gov_action_t::update_committee_t {};
        expect(st.committee_accepted(action));
        action.proposal.action.val = gov_action_t::no_confidence_t {};
        expect(st.committee_accepted(action));
        action.proposal.action.val = gov_action_t::new_constitution_t {};
        expect(!st.committee_accepted(action));
    };

    "Treasury bounds use mathematical sums and reject before mutation"_test = [] {
        enact_state_t st {};
        gov_action_t action {};
        gov_action_t::treasury_withdrawals_t wdrl {};
        wdrl.withdrawals.emplace(gap_reward(1), UINT64_MAX);
        wdrl.withdrawals.emplace(gap_reward(2), 1);
        action.val = wdrl;
        expect(!rules::ratify::withdrawals_can_withdraw(action, st));
        expect(!static_cast<bool>(rules::enact::treasury_withdrawal(st, {}, wdrl)));
        expect(st.withdrawals.empty());
        expect(st.treasury == 0_u);
        wdrl.withdrawals.clear();
        wdrl.withdrawals.emplace(gap_reward(1), 4);
        st.treasury = 4;
        expect(static_cast<bool>(rules::enact::treasury_withdrawal(st, {}, wdrl)));
        expect(st.treasury == 0_u);
        expect(st.withdrawals.at(static_cast<stake_ident>(gap_reward(1))) == 4_u);
        expect(!static_cast<bool>(rules::enact::treasury_withdrawal(st, {}, wdrl)));
        st.treasury = 1;
        st.withdrawals[static_cast<stake_ident>(gap_reward(1))] = UINT64_MAX;
        wdrl.withdrawals.begin()->second = 1;
        expect(!static_cast<bool>(rules::enact::treasury_withdrawal(st, {}, wdrl)));
        expect(st.treasury == 1_u);
        expect(st.withdrawals.at(static_cast<stake_ident>(gap_reward(1))) == UINT64_MAX);
    };

    "GOVCERT resignation accepts eligible cold credentials without hot keys"_test = [] {
        gap_test_state st {};
        const stake_ident cold = gap_reward(1);
        st._enact_state.committee.emplace();
        st._enact_state.committee->members.emplace(cold, 100);
        resign_committee_cold_cert cert {};
        cert.cold_id = cold;
        st.process_cert(cert, {});
        expect(std::holds_alternative<committee_t::resigned_t>(st._committee_hot_keys.at(cold).val));
        expect(throws([&] { st.process_cert(cert, {}); }));
        gap_test_state proposed {};
        proposed._enact_state.committee.reset();
        gov_action_t::update_committee_t update {};
        update.members_to_add.emplace(cold, 100);
        proposed._proposals[gov_action_id_t {}].proposal.action.val = update;
        proposed.process_cert(cert, {});
        expect(std::holds_alternative<committee_t::resigned_t>(proposed._committee_hot_keys.at(cold).val));
        cert.cold_id = static_cast<stake_ident>(gap_reward(2));
        expect(throws([&] { proposed.process_cert(cert, {}); }));
    };

    "DELEG checks rewards and exact recorded refund before mutation"_test = [] {
        gap_test_state st {};
        const stake_ident id = gap_reward(1);
        st.seed(id, 1);
        expect(throws([&] { st.process_cert(unreg_cert { id, 10 }, {}); }));
        expect(st._accounts.at(id).ptr.has_value());
        expect(st._accounts.at(id).reward == 1_u);
        st._accounts.at(id).reward = 0;
        expect(throws([&] { st.process_cert(unreg_cert { id, 9 }, {}); }));
        expect(st._accounts.at(id).deposit == 10_u);
        st._params.key_deposit = 20;
        st.process_cert(stake_dereg_cert { id }, {});
        expect(!st._accounts.at(id).ptr.has_value());
        expect(st._deposited == 0_u);
        expect(throws([&] { st.process_cert(stake_dereg_cert { id }, {}); }));
    };

    "DELEG registration and combined delegation have distinct deposit premises"_test = [] {
        gap_test_state st {};
        const stake_ident id = gap_reward(1);
        expect(throws([&] { st.process_cert(reg_cert { id, 9 }, {}); }));
        expect(!st.has_stake(id));
        st.process_cert(stake_reg_cert { id }, {});
        expect(st._accounts.at(id).deposit == 10_u);
        expect(throws([&] { st.process_cert(reg_cert { id, 10 }, {}); }));
        st._register_delegating_stake(id, 0, {});
        expect(st._accounts.at(id).deposit == 10_u);
        expect(throws([&] { st._register_delegating_stake(id, 10, {}); }));
    };

    "GOV uses final registration across certificate changes and transaction boundaries"_test = [] {
        const stake_ident id = gap_reward(1);
        const cert_loc_t tx { 0, 1, 0 };
        for (const auto major: { 9U, 10U, 11U }) {
            gap_test_state st {};
            st._params.protocol_ver.major = major;
            const auto allowed_unregistered = major == 9;
            // Cached account data alone must not count as registration.
            st._accounts[id].stake = 100;
            expect(st._proposal_valid(st.proposal_for(id), tx) == allowed_unregistered);
            st.process_cert(reg_cert { id, 10 }, tx);
            expect(st._proposal_valid(st.proposal_for(id), tx));
            st.process_cert(unreg_cert { id, 10 }, tx);
            expect(st._proposal_valid(st.proposal_for(id), tx) == allowed_unregistered);
            st.process_cert(reg_cert { id, 10 }, tx);
            expect(st._proposal_valid(st.proposal_for(id), tx));
            const cert_loc_t next { 0, 2, 0 };
            st.process_cert(unreg_cert { id, 10 }, next);
            expect(st._proposal_valid(st.proposal_for(id), next) == allowed_unregistered);
            expect(st._proposal_valid(st.proposal_for(id), cert_loc_t { 0, 3, 0 }) == allowed_unregistered);
        }
    };

    "GOV treasury recipients use final account registration"_test = [] {
        const stake_ident return_id = gap_reward(1);
        for (const auto major: { 10U, 11U }) {
            gap_test_state st {};
            st._params.protocol_ver.major = major;
            st.seed(return_id);
            auto p = st.proposal_for(return_id);
            gov_action_t::treasury_withdrawals_t wdrl {};
            auto recipient = gap_reward(2);
            recipient[0] = 0xE0 | p.procedure.return_addr_network_id;
            wdrl.policy_id = st._enact_state.constitution.policy_id;
            wdrl.withdrawals.emplace(recipient, 1);
            p.procedure.action.val = wdrl;
            const cert_loc_t tx { 0, 1, 0 };
            expect(!st._proposal_valid(p, tx));
            st.process_cert(reg_cert { static_cast<stake_ident>(recipient), 10 }, tx);
            expect(st._proposal_valid(p, tx));
            const cert_loc_t next { 0, 2, 0 };
            st.process_cert(unreg_cert { static_cast<stake_ident>(recipient), 10 }, next);
            expect(!st._proposal_valid(p, next));
            expect(!st._proposal_valid(p, cert_loc_t { 0, 3, 0 }));
        }
    };

    "GOV rejects unregistered treasury recipients independently of return account"_test = [] {
        protocol_params params {};
        params.protocol_ver = { 11, 0 };
        enact_state_t enact {};
        proposal_map proposals {};
        proposal_t proposal {};
        proposal.procedure.return_addr_network_id = 1;
        gov_action_t::treasury_withdrawals_t wdrl {};
        wdrl.withdrawals.emplace(gap_reward(1), 1);
        proposal.procedure.action.val = wdrl;
        rules::gov::propose_environment env {
            0, proposal.procedure.deposit, 1, params, enact, proposals, true, false
        };
        const auto rejected = rules::gov::propose(env, proposal);
        expect(!static_cast<bool>(rejected));
        expect(rejected.failure == rules::gov::failure::action_invalid);
        env.withdrawal_accounts_registered = true;
        expect(static_cast<bool>(rules::gov::propose(env, proposal)));
        env.return_account_registered = false;
        expect(rules::gov::propose(env, proposal).failure == rules::gov::failure::unregistered_return_account);
    };

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

        const credential_t another_cold {
            crypto::blake2b::digest<key_hash>(std::string_view { "another cold" }), false
        };
        hot_keys[another_cold].val = hot_id;
        for (const auto &elected: { cold_id, another_cold }) {
            enact_state.committee->members.clear();
            enact_state.committee->members.emplace(elected, 10);
            expect(static_cast<bool>(rules::gov::vote(env, procedure)));
        }
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
