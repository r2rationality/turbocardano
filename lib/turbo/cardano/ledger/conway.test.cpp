/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/ledger/conway.hpp>
#include <turbo/cardano/ledger/state.hpp>
#include <turbo/common/test.hpp>
#include <turbo/index/timed-update.hpp>

namespace {
    using namespace turbo;
    using namespace cardano;
    using namespace ledger::conway;

    struct test_state: state {
        void protocol_ver(const protocol_version &pv)
        {
            _params.protocol_ver = pv;
            _params_prev.protocol_ver = pv;
            _enact_state.params.protocol_ver = pv;
            _enact_state.prev_params.protocol_ver = pv;
            _ratify_state.new_state.params.protocol_ver = pv;
            _ratify_state.new_state.prev_params.protocol_ver = pv;
        }

        const optional_script_t &constitution_policy_id() const
        {
            return _enact_state.constitution.policy_id;
        }

        void populate_pool_vrf_key_hashes()
        {
            _populate_pool_vrf_key_hashes();
        }

        uint64_t pool_vrf_key_occurrences(const vrf_vkey &vrf) const
        {
            if (const auto it = _pool_vrf_key_hashes.find(vrf); it != _pool_vrf_key_hashes.end())
                return it->second;
            return 0;
        }

        void process_tx_prelude(
            const index::timed_update::conway_tx_prelude &prelude,
            const cert_loc_t &loc)
        {
            _process_tx_prelude(prelude, loc);
        }

        void prepare_redelegated_reward(
            const stake_ident &stake_id,
            const pool_hash &reward_time_pool,
            const pool_hash &boundary_pool,
            const uint64_t stake,
            const uint64_t reward)
        {
            _active_pool_params.try_emplace(reward_time_pool);
            _active_pool_params.try_emplace(boundary_pool);
            _active_pool_dist.create(reward_time_pool);
            _active_pool_dist.create(boundary_pool);
            _active_pool_dist.add(boundary_pool, stake);
            _active_inv_delegs[boundary_pool].emplace(stake_id);

            auto &account = _accounts[stake_id];
            account.stake = stake;
            account.ptr.emplace(0, 0, 0);
            account.deleg = boundary_pool;

            _potential_rewards[stake_id].emplace(
                ledger::reward_type::member,
                reward_time_pool,
                reward,
                reward_time_pool);
        }
    };
}

suite cardano_ledger_conway_suite = [] {
    using boost::ext::ut::v2_1_0::nothrow;
    "cardano::ledger::conway::timed_update_order"_test = [] {
        using turbo::index::timed_update::item;
        std::vector<item> updates {
            { cert_loc_t { 10, 2, 0 }, cardano::conway::vote_info_t {} },
            { cert_loc_t { 10, 2, 0 }, turbo::index::timed_update::stake_withdraw {} },
            { cert_loc_t { 10, 2, 0 }, turbo::index::timed_update::conway_tx_prelude {} },
            { cert_loc_t { 10, 2, 24 }, proposal_t {} },
            { cert_loc_t { 10, 2, 1 }, reg_cert {} },
            { cert_loc_t { 10, 2, 23 }, proposal_t {} },
            { cert_loc_t { 10, 2, 0 }, reg_drep_cert {} }
        };
        std::sort(updates.begin(), updates.end());
        expect_equal(0, updates[0].loc.cert_idx);
        expect(std::holds_alternative<turbo::index::timed_update::conway_tx_prelude>(updates[0].update));
        expect_equal(0, updates[1].loc.cert_idx);
        expect(std::holds_alternative<turbo::index::timed_update::stake_withdraw>(updates[1].update));
        expect_equal(0, updates[2].loc.cert_idx);
        expect(std::holds_alternative<reg_drep_cert>(updates[2].update));
        expect_equal(1, updates[3].loc.cert_idx);
        expect(std::holds_alternative<reg_cert>(updates[3].update));
        expect_equal(23, updates[4].loc.cert_idx);
        expect(std::holds_alternative<proposal_t>(updates[4].update));
        expect_equal(24, updates[5].loc.cert_idx);
        expect(std::holds_alternative<proposal_t>(updates[5].update));
        expect_equal(0, updates[6].loc.cert_idx);
        expect(std::holds_alternative<cardano::conway::vote_info_t>(updates[6].update));
    };
    "cardano::ledger::conway::state"_test = [] {
        const credential_t id0 { crypto::blake2b::digest<key_hash>(std::string_view { "0" }), false };
        "protocol version 11 remains in Conway"_test = [] {
            expect_equal(7, protocol_version { 11, 0 }.era());
        };
        "protocol version 11 rejects duplicate pool VRF keys"_test = [] {
            test_state st {};
            st.protocol_ver({ 11, 0 });
            const auto pool_a = crypto::blake2b::digest<pool_hash>(std::string_view { "pool-a" });
            const auto pool_b = crypto::blake2b::digest<pool_hash>(std::string_view { "pool-b" });
            pool_params params {};
            params.vrf_vkey = crypto::blake2b::digest<vrf_vkey>(std::string_view { "shared-vrf" });
            expect(nothrow([&] { st.register_pool({ pool_a, params }); }));
            expect(throws([&] { st.register_pool({ pool_b, params }); }));
            expect_equal(1, st.pool_vrf_key_occurrences(params.vrf_vkey));
            expect(nothrow([&] { st.register_pool({ pool_a, params }); }));
            expect_equal(2, st.pool_vrf_key_occurrences(params.vrf_vkey));
            expect(nothrow([&] { st.register_pool({ pool_a, params }); }));
            expect_equal(2, st.pool_vrf_key_occurrences(params.vrf_vkey));
            const auto original_vrf = params.vrf_vkey;
            params.vrf_vkey = crypto::blake2b::digest<vrf_vkey>(std::string_view { "replacement-vrf" });
            expect(nothrow([&] { st.register_pool({ pool_a, params }); }));
            expect_equal(1, st.pool_vrf_key_occurrences(original_vrf));
            expect_equal(1, st.pool_vrf_key_occurrences(params.vrf_vkey));
        };
        "protocol version 11 migration counts existing pool VRF keys"_test = [] {
            test_state st {};
            st.protocol_ver({ 10, 0 });
            const auto pool_a = crypto::blake2b::digest<pool_hash>(std::string_view { "pool-a" });
            const auto pool_b = crypto::blake2b::digest<pool_hash>(std::string_view { "pool-b" });
            pool_params params {};
            params.vrf_vkey = crypto::blake2b::digest<vrf_vkey>(std::string_view { "shared-vrf" });
            st.register_pool({ pool_a, params });
            st.register_pool({ pool_b, params });
            st.populate_pool_vrf_key_hashes();
            expect_equal(2, st.pool_vrf_key_occurrences(params.vrf_vkey));
        };
        "protocol version 11 updates dormant DReps in the transaction prelude"_test = [&] {
            const auto &cfg = cardano::config::get();
            test_state st {};
            st.protocol_ver({ 11, 0 });
            st.process_cert(reg_drep_cert { id0, st.params().drep_deposit }, cert_loc_t { 0, 0, 0 });
            st.start_epoch({ 1 });
            expect_equal(1, st.num_dormant_epochs());
            expect_equal(20, st.drep_state().at(id0).expire_epoch);
            st.process_tx_prelude(
                index::timed_update::conway_tx_prelude { .has_proposals=true },
                cert_loc_t { slot::from_epoch(1, cfg), 0, 0 });
            expect_equal(0, st.num_dormant_epochs());
            expect_equal(21, st.drep_state().at(id0).expire_epoch);
        };
        "babbage transition starts with first conway epoch dormant"_test = [] {
            auto &cfg = cardano::config::get();
            auto &sched = scheduler::get();
            ledger::shelley::state shelley_st { cfg, sched };
            ledger::alonzo::state alonzo_st { std::move(shelley_st) };
            ledger::babbage::state babbage_st { std::move(alonzo_st) };
            ledger::conway::state st { std::move(babbage_st) };
            expect_equal(1, st.num_dormant_epochs());
        };
        "rewards follow the delegation at the epoch boundary"_test = [] {
            test_state st {};
            const stake_ident stake_id {
                crypto::blake2b::digest<key_hash>(std::string_view { "stake" }),
                false
            };
            const auto reward_time_pool = crypto::blake2b::digest<pool_hash>(std::string_view { "old-pool" });
            const auto boundary_pool = crypto::blake2b::digest<pool_hash>(std::string_view { "new-pool" });
            st.prepare_redelegated_reward(stake_id, reward_time_pool, boundary_pool, 100, 45);

            st.start_epoch({ 1 });

            const auto &pool_power = st.pulser_data().pool_voting_power;
            expect_equal(0, pool_power.get(reward_time_pool));
            expect_equal(145, pool_power.get(boundary_pool));
        };
        "proposal resets dormant epochs and extends active dreps"_test = [&] {
            const auto &cfg = cardano::config::get();
            state st {};
            st.process_cert(reg_drep_cert { id0, st.params().drep_deposit }, cert_loc_t { 0, 0, 1 });
            const stake_ident return_addr { crypto::blake2b::digest<key_hash>(std::string_view { "R" }), false };
            expect_equal(0, st.num_dormant_epochs());
            st.start_epoch({ 1 });
            expect_equal(1, st.num_dormant_epochs());
            expect_equal(20, st.drep_state().at(id0).expire_epoch);
            proposal_t p {};
            p.id.tx_id = crypto::blake2b::digest<tx_hash>(std::string_view { "A" });
            p.procedure.deposit = st.params().gov_action_deposit;
            p.procedure.return_addr = return_addr;
            p.procedure.return_addr_network_id = cfg.shelley_network_id;
            p.procedure.action.val = gov_action_t::info_action_t {};
            st.process_proposal(p, cert_loc_t { slot::from_epoch(1, cfg), 0, 0 });
            expect_equal(0, st.num_dormant_epochs());
            expect_equal(21, st.drep_state().at(id0).expire_epoch);
            st.start_epoch({ 2 });
            expect_equal(0, st.num_dormant_epochs());
        };
        "reg/unreg drep"_test = [&] {
            state st {};

            expect(!st.has_drep(id0));
            st.process_cert(reg_drep_cert { id0, st.params().drep_deposit }, cert_loc_t { 0, 0, 1 });
            expect(st.has_drep(id0));
            st.process_cert(unreg_drep_cert { id0, st.params().drep_deposit }, cert_loc_t { 1, 0, 0 });
            expect(!st.has_drep(id0));
        };
        "drep expiry uses certificate slot epoch"_test = [&] {
            const auto &cfg = cardano::config::get();
            state st {};
            st.process_cert(reg_drep_cert { id0, st.params().drep_deposit }, cert_loc_t { slot::from_epoch(1, cfg), 0, 1 });
            expect_equal(21, st.drep_state().at(id0).expire_epoch);
        };
        "drep vote refreshes expiry"_test = [&] {
            const auto &cfg = cardano::config::get();
            state st {};
            const stake_ident return_addr { crypto::blake2b::digest<key_hash>(std::string_view { "R" }), false };
            st.process_cert(reg_cert { return_addr, st.params().key_deposit }, cert_loc_t { 0, 0, 1 });
            st.process_cert(reg_drep_cert { id0, st.params().drep_deposit }, cert_loc_t { 0, 0, 2 });
            expect_equal(20, st.drep_state().at(id0).expire_epoch);
            const gov_action_id_t gid { crypto::blake2b::digest<tx_hash>(std::string_view { "A" }), 0 };
            st.process_proposal(
                proposal_t {
                    gid,
                    proposal_procedure_t {
                        st.params().gov_action_deposit,
                        return_addr,
                        cfg.shelley_network_id,
                        { gov_action_t::info_action_t {} }
                    }
                },
                cert_loc_t { 0, 0, 0 }
            );
            st.start_epoch({ 1 });
            st.process_vote(
                vote_info_t {
                    voter_t { voter_t::drep_key, id0.hash },
                    gid,
                    voting_procedure_t { vote_t::yes }
                },
                cert_loc_t { slot::from_epoch(1, cfg) + 1, 0, 0 }
            );
            expect_equal(21, st.drep_state().at(id0).expire_epoch);
        };
        "reg/unreg stake"_test = [&] {
            state st {};
            expect(!st.has_stake(id0));
            st.process_cert(reg_cert { id0 }, cert_loc_t { 0, 0, 1 });
            expect(st.has_stake(id0));
            st.process_cert(unreg_cert { id0 }, cert_loc_t { 1, 0, 0 });
            expect(!st.has_drep(id0));
        };
        "update_committee is disallowed during bootstrap"_test = [] {
            state st {};
            expect(st.committee().has_value());
            if (const auto &cc = st.committee(); cc) {
                expect_equal(7, cc->members.size());
                expect_equal(rational_u64 { 2, 3 }, cc->threshold);
            }
            {
                const stake_ident return_addr { crypto::blake2b::digest<key_hash>(std::string_view { "A" }), false };
                st.process_cert(reg_cert { return_addr, st.params().key_deposit }, cert_loc_t { 0, 0, 1 });
                const gov_action_id_t gid { crypto::blake2b::digest<tx_hash>(std::string_view { "A" }), 0 };
                proposal_procedure_t pp {};
                pp.deposit = st.params().gov_action_deposit;
                pp.return_addr = return_addr;
                pp.return_addr_network_id = cardano::config::get().shelley_network_id;
                {
                    gov_action_t::update_committee_t c_upd {};
                    c_upd.members_to_remove.emplace(credential_t { script_hash::from_hex("df0e83bde65416dade5b1f97e7f115cc1ff999550ad968850783fe50"), true });
                    c_upd.new_threshold = { 5, 6 };
                    pp.action = gov_action_t { std::move(c_upd) };
                }
                const proposal_t p { gid, std::move(pp) };
                expect(throws([&] { st.process_proposal(p, cert_loc_t { 0, 0, 0 }); }));
            }
            expect(st.committee().has_value());
            if (const auto &cc = st.committee(); cc) {
                expect_equal(7, cc->members.size());
                expect_equal(rational_u64 { 2, 3 }, cc->threshold);
            }
        };
        "parameter update"_test = [] {
            const auto &cfg = cardano::config::get();
            cfg.shelley_start_epoch(0U);
            state st {};
            gov_action_id_t gid {};
            gid.tx_id = crypto::blake2b::digest<tx_hash>(std::string_view { "A" });
            const stake_ident return_addr { crypto::blake2b::digest<key_hash>(std::string_view { "R" }), false };
            st.process_cert(reg_cert { return_addr, st.params().key_deposit }, cert_loc_t { 0, 0, 1 });
            proposal_t p {};
            p.id = gid;
            p.procedure.deposit = st.params().gov_action_deposit;
            p.procedure.return_addr = return_addr;
            p.procedure.return_addr_network_id = cardano::config::get().shelley_network_id;
            const cert_loc_t p_loc { 0, 0, 0 };
            st.process_proposal(p, p_loc);
            {
                const auto &ga_st = st.gov_action(gid);
                expect_equal(0, ga_st.drep_votes.size());
            }

            const credential_t drep_id { crypto::blake2b::digest<key_hash>(std::string_view { "B" }), false };
            voter_t voter {};
            voter.type = voter_t::drep_key;
            voter.hash = drep_id.hash;
            const cert_loc_t v_loc { 1, 0, 0 };
            const voting_procedure_t vote_proc { vote_t::yes, {} };
            const vote_info_t vp { voter, gid, vote_proc };
            expect(throws([&] { st.process_vote(vp, v_loc); }));
            expect(throws([&] {
                st.process_vote(vote_info_t { voter, gov_action_id_t { crypto::blake2b::digest<tx_hash>(std::string_view { "missing" }), 0 }, vote_proc }, v_loc);
            }));

            st.process_cert(reg_drep_cert { drep_id, st.params().drep_deposit }, cert_loc_t { 0, 0, 1 });
            expect(nothrow([&] { st.process_vote(vp, v_loc); }));
            {
                const auto &ga_st = st.gov_action(gid);
                expect_equal(1, ga_st.drep_votes.size());
            }
            const auto ga_lifetime = st.params().gov_action_lifetime;
            expect_equal(6, ga_lifetime);
            st.process_block(100, 1); // needed so that start_epoch accepts progress
            for (size_t e = 0; e < ga_lifetime + 1; ++e) {
                st.start_epoch({});
                expect(st.has_gov_action(gid));
                st.process_block(100, slot::from_epoch(e + 1, cfg.shelley_rewards_ready_slot, cfg)); // needed so that start_epoch accepts progress
                st.run_pulser_if_ready();
            }
            st.start_epoch({});
            expect(!st.has_gov_action(gid));
        };
        "treasury withdrawal proposal may target an unregistered reward account"_test = [] {
            const auto &cfg = cardano::config::get();
            test_state st {};
            st.protocol_ver({ 10, 0 });
            reward_id_t reward_id {};
            reward_id[0] = 0xE0 | cfg.shelley_network_id;
            const auto reward_hash = crypto::blake2b::digest<key_hash>(std::string_view { "W" });
            memcpy(reward_id.data() + 1, reward_hash.data(), reward_hash.size());
            const gov_action_id_t gid { crypto::blake2b::digest<tx_hash>(std::string_view { "T" }) };
            const stake_ident return_addr { crypto::blake2b::digest<key_hash>(std::string_view { "R" }), false };
            st.process_cert(reg_cert { return_addr, st.params().key_deposit }, cert_loc_t { 0, 0, 1 });
            proposal_t p {};
            p.id = gid;
            p.procedure.deposit = st.params().gov_action_deposit;
            p.procedure.return_addr = return_addr;
            p.procedure.return_addr_network_id = cfg.shelley_network_id;
            gov_action_t::treasury_withdrawals_t withdrawals {};
            withdrawals.withdrawals.emplace(reward_id, 2770581);
            withdrawals.policy_id = st.constitution_policy_id();
            p.procedure.action.val = std::move(withdrawals);
            expect(nothrow([&] { st.process_proposal(p, cert_loc_t { 0, 0, 0 }); }));
        };
        "committee voting"_test = [] {
            state st {};
            gov_action_id_t gid { crypto::blake2b::digest<tx_hash>(std::string_view { "A" }) };
            const stake_ident return_addr { crypto::blake2b::digest<key_hash>(std::string_view { "R" }), false };
            st.process_cert(reg_cert { return_addr, st.params().key_deposit }, cert_loc_t { 0, 0, 1 });
            st.process_proposal(
                proposal_t {
                    gid,
                    proposal_procedure_t {
                        st.params().gov_action_deposit,
                        return_addr,
                        cardano::config::get().shelley_network_id,
                        { gov_action_t::info_action_t {} }
                    }
                },
                cert_loc_t { 0, 0, 0 }
            );
            const credential_t drep_id { crypto::blake2b::digest<key_hash>(std::string_view { "B" }), false };
            st.process_cert(reg_drep_cert { drep_id, st.params().drep_deposit }, cert_loc_t { 0, 0, 1 });
            st.process_vote(
                vote_info_t {
                    voter_t { voter_t::drep_key, drep_id.hash },
                    gid,
                    voting_procedure_t { vote_t::yes }
                },
                cert_loc_t { 1, 0, 0 }
            );
            st.start_epoch({});
            const auto &gas = st.pulser_data().proposals.at(gid);
            expect_equal(false, st.committee_accepted(gas));
            expect_equal(false, st.pools_accepted(gas));
            expect_equal(false, st.dreps_accepted(gas));
        };
    };
    "cardano::ledger::conway::vrf_state"_test = [] {
        "max_epoch_slot"_test = [] {
            ledger::conway::vrf_state st { ledger::babbage::vrf_state { ledger::shelley::vrf_state {} } };
            expect_equal(432000 - 172800, st.max_epoch_slot());
        };
        "cbor load/save"_test = [] {
            ledger::conway::vrf_state st { ledger::babbage::vrf_state { ledger::shelley::vrf_state {} } };
            const auto exp_cbor = file::read(install_path("data/ledger/conway-vrf-state.cbor"));
            st.from_cbor(cbor::zero2::parse(exp_cbor).get());
            ledger::cbor_encoder enc { []{ return era_encoder { era_t::conway }; } };
            st.to_cbor(enc);
            enc.run(scheduler::get(), "vrf_state::to_cbor");
            const auto act_cbor = enc.flat();
            expect_equal(exp_cbor.size(), act_cbor.size());
            expect_equal(static_cast<buffer>(exp_cbor), act_cbor);
        };
    };
};
