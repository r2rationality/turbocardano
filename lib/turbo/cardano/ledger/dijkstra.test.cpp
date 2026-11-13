/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/ledger/dijkstra.hpp>
#include <turbo/cardano/ledger/state.hpp>
#include <turbo/cardano/ledger/rules/gov/conway.hpp>
#include <turbo/common/test.hpp>
#include <turbo/zpp.hpp>

namespace {
    using namespace turbo;
    using namespace cardano;

    struct source_state: ledger::conway::state {
        using state::state;
        using state::_params;
        using state::_params_prev;
        using state::_enact_state;
        using state::_ratify_state;
        using state::_ratify_ready;
        using state::_accounts;
        using state::_active_pool_params;
        using state::_pool_vrf_key_hashes;
    };

    struct target_state: ledger::dijkstra::state {
        using state::state;
        using state::_params_prev;
        using state::_enact_state;
        using state::_ratify_state;
        using state::_ratify_ready;
        using state::_accounts;
        using state::_pool_vrf_key_hashes;
        using state::_enact_proposal;
        using state::_gov_enact;
    };

    template<typename Base>
    struct epoch_state: Base {
        using Base::Base;

        void prepare_payout(const uint64_t major, const uint64_t next_major)
        {
            this->_params.protocol_ver = { major, 0 };
            this->_params_prev = this->_params;
            this->_enact_state.params = this->_params;
            this->_enact_state.prev_params = this->_params;
            this->_ratify_state.new_state = this->_enact_state;
            this->_ratify_state.new_state.params.protocol_ver = { next_major, 0 };
            this->_ratify_state.new_state.withdrawals.add(stake_ident {}, 100);
            this->_ratify_ready = true;
            this->_treasury = 100;
            auto &account = this->_accounts[stake_ident {}];
            account.ptr.emplace(0, 0, 0);
            account.stake = 1'000;
            account.deleg = pool_hash {};
            this->_active_inv_delegs[pool_hash {}].emplace(stake_ident {});
            this->_active_pool_params.try_emplace(pool_hash {});
            this->_active_pool_dist.create(pool_hash {});
            this->_active_pool_dist.add(pool_hash {}, account.stake);
            this->rotate_snapshots();
        }

        uint64_t mark_stake() const { return this->_accounts.at(stake_ident {}).mark_stake; }
        uint64_t mark_pool_stake() const { return this->_mark.pool_dist.get(pool_hash {}); }
        uint64_t operating_stake() const { return this->_operating_stake_dist.at(pool_hash {}).active_stake; }
    };
}

suite cardano_ledger_dijkstra_suite = [] {
    "cardano::ledger::dijkstra"_test = [] {
        "epoch payouts enter Dijkstra mark before becoming the next epoch's operating stake"_test = [] {
            const configs_dir files { configs_dir::network_path("dijkstra") };
            const cardano::config cfg { files };
            const auto check = [](auto &state, const uint64_t expected_mark) {
                state.start_epoch(1);
                expect_equal(state.mark_stake(), expected_mark);
                expect_equal(state.mark_pool_stake(), expected_mark);
                expect_equal(state.operating_stake(), 1'000);
                state.start_epoch(2);
                expect_equal(state.mark_stake(), 1'100);
                expect_equal(state.operating_stake(), expected_mark);
                state.start_epoch(3);
                expect_equal(state.operating_stake(), 1'100);
            };
            epoch_state<ledger::conway::state> conway { cfg };
            conway.prepare_payout(11, 11);
            check(conway, 1'000);
            epoch_state<ledger::conway::state> transition { cfg };
            transition.prepare_payout(11, 12);
            expect(transition.next_protocol_version() == protocol_version { 12, 0 });
            epoch_state<ledger::dijkstra::state> translated { std::move(transition) };
            check(translated, 1'100);
            epoch_state<ledger::dijkstra::state> dijkstra { ledger::conway::state { cfg } };
            dijkstra.prepare_payout(12, 12);
            check(dijkstra, 1'100);
        };
        "the hard fork applies Dijkstra snapshot rules at its first epoch boundary"_test = [] {
            const configs_dir files { configs_dir::network_path("dijkstra") };
            const cardano::config cfg { files };
            epoch_state<ledger::conway::state> source { cfg };
            source.prepare_payout(11, 12);
            ledger::shelley::vrf_state vrf { cfg };
            const file::tmp snapshot { "dijkstra-first-epoch" };
            ledger::zpp_encoder enc {};
            enc.add([](auto) { return turbo::zpp::serialize(ledger::subchain_list {}); });
            enc.add([](auto) { return turbo::zpp::serialize(ledger::era_list(7, 0)); });
            source.to_zpp(enc);
            vrf.to_zpp(enc);
            enc.run(scheduler::get(), "encode-dijkstra-first-epoch");
            enc.save(snapshot.path(), true);

            ledger::state state { cfg };
            state.load_zpp(snapshot.path());
            state.start_epoch(1);
            expect_equal(state.params().protocol_ver.major, 12);
            expect_equal(state.eras().size(), 8);
            expect_equal(state.pool_stake_dist().at(pool_hash {}).active_stake, 1'000);
            state.start_epoch(2);
            expect_equal(state.pool_stake_dist().at(pool_hash {}).active_stake, 1'100);
        };
        "Praos certificate counters permit only the current or next issue"_test = [] {
            for (const auto initial: { uint64_t { 0 }, uint64_t { 1 }, uint64_t { 2 } }) {
                ledger::shelley::vrf_state state {};
                index::vrf::item item { .era=8, .kes_counter=initial };
                if (initial > 1) {
                    expect(throws([&] { state.process_updates({ item }); }));
                    continue;
                }
                expect(nothrow([&] { state.process_updates({ item }); }));
                ++item.slot;
                expect(nothrow([&] { state.process_updates({ item }); }));
                ++item.kes_counter;
                ++item.slot;
                expect(nothrow([&] { state.process_updates({ item }); }));
                item.kes_counter += 2;
                ++item.slot;
                expect(throws([&] { state.process_updates({ item }); }));
            }
        };
        "transition requires Dijkstra genesis"_test = [] {
            expect(throws([] {
                ledger::dijkstra::state state { ledger::conway::state {} };
            }));
        };
        "translation preserves ledger data and upgrades every parameter snapshot"_test = [] {
            const configs_dir files { configs_dir::network_path("dijkstra") };
            const cardano::config cfg { files };
            source_state source { cfg };
            source._params.protocol_ver = { 12, 0 };
            source._params.min_fee_a = 45;
            source._params_prev = source._params;
            source._params_prev.protocol_ver = { 11, 0 };
            source._params_prev.min_fee_a = 44;
            source._enact_state.params = source._params;
            source._enact_state.prev_params = source._params_prev;
            source._ratify_state.new_state = source._enact_state;
            source._ratify_ready = false;
            source._accounts[stake_ident {}].reward = 123;
            source._active_pool_params[pool_hash {}].params.vrf_vkey = vrf_vkey {};
            source._pool_vrf_key_hashes[vrf_vkey {}] = 99;
            const auto old_costs = source.params().plutus_cost_models;
            target_state state { std::move(source) };
            expect(state._ratify_ready);
            expect_equal(state.params().min_fee_a, 45);
            expect_equal(state._params_prev.min_fee_a, 44);
            expect_equal(state.params().protocol_ver.major, 12);
            expect_equal(state._params_prev.protocol_ver.major, 11);
            expect_equal(state._accounts.at(stake_ident {}).reward, 123);
            expect_equal(state._pool_vrf_key_hashes.at(vrf_vkey {}), 1);
            for (const auto *p: std::array<const protocol_params *, 6> { &state.params(), &state._params_prev,
                    &state._enact_state.params, &state._enact_state.prev_params,
                    &state._ratify_state.new_state.params, &state._ratify_state.new_state.prev_params }) {
                const auto &g = *cfg.dijkstra_genesis;
                expect_equal(p->max_ref_script_size_per_block, g.max_ref_script_size_per_block);
                expect_equal(p->leios_committee_size, g.leios_committee_size);
                expect_equal(p->max_endorser_block_ex_units, g.max_endorser_block_execution_units);
                expect(p->peras_bootstrap_round == g.peras_bootstrap_round);
                expect(p->min_pool_margin == g.min_pool_margin);
                expect(!p->max_pledge_leverage);
                expect(p->plutus_cost_models.at(3).raw_values() == g.plutus_v4_cost_model);
                for (const auto &[language, costs]: old_costs.items)
                    expect(p->plutus_cost_models.at(language) == costs);
            }

            param_update_t update {};
            update.leios_committee_size = 1234;
            update.max_endorser_block_txs_size = 987654;
            update.peras_bootstrap_round.emplace();
            update.plutus_cost_models.emplace();
            update.plutus_cost_models->items.emplace(3, plutus_cost_model { { 5, -7, 9 } });
            const gov_action_t action { gov_action_t::parameter_change_t { {}, update, {} } };
            state._enact_proposal(state._ratify_state.new_state, {}, action);
            state._gov_enact();
            expect_equal(state.params().leios_committee_size, 1234);
            expect_equal(state._params_prev.leios_committee_size, cfg.dijkstra_genesis->leios_committee_size);
            const file::tmp snapshot { "dijkstra-parameters" };
            ledger::zpp_encoder enc {};
            state.to_zpp(enc);
            enc.run(scheduler::get(), "encode-dijkstra-parameters");
            enc.save(snapshot.path(), true);
            target_state restored { ledger::conway::state { cfg } };
            ledger::parallel_decoder dec { snapshot.path() };
            restored.from_zpp(dec);
            dec.run(scheduler::get(), "decode-dijkstra-parameters");
            expect(restored.params() == state.params());
            expect(restored._params_prev == state._params_prev);
            expect(restored._enact_state.params == state._enact_state.params);
            expect(restored._ratify_state.new_state.params == state._ratify_state.new_state.params);
            expect(!restored.params().peras_bootstrap_round);
            expect(restored.params().plutus_cost_models.at(3).raw_values() == std::vector<int64_t> { 5, -7, 9 });
        };
        "new parameter updates apply, persist and select their governance voting groups"_test = [] {
            const auto accepted = [](const param_update_t &update, const uint64_t major) {
                protocol_params params {};
                params.protocol_ver = { major, 0 };
                ledger::conway::enact_state_t enact {};
                ledger::conway::proposal_map proposals {};
                proposal_t proposal {};
                proposal.procedure.return_addr_network_id = 1;
                proposal.procedure.deposit = params.gov_action_deposit;
                proposal.procedure.action.val = gov_action_t::parameter_change_t { {}, update, {} };
                const ledger::conway::rules::gov::propose_environment env {
                    0, params.gov_action_deposit, 1, params, enact, proposals, true, true
                };
                return static_cast<bool>(ledger::conway::rules::gov::propose(env, proposal));
            };
            const auto check = [&](const auto update_member, const auto param_member, const auto value, const bool network) {
                param_update_t update {};
                (update.*update_member).emplace(value);
                expect(update.security_group());
                expect(update.network_group() == network);
                expect(update.economic_group() == !network);
                expect(!update.technical_group());
                expect(!update.governance_group());
                expect(accepted(update, 12));
                expect(!accepted(update, 11));
                protocol_params params {};
                params.protocol_ver = { 12, 0 };
                params.apply(update);
                expect(params.*param_member == value);
                const auto restored = turbo::zpp::deserialize<protocol_params>(turbo::zpp::serialize(params));
                expect(restored.*param_member == value);
                expect(restored == params);
            };
            check(&param_update_t::leios_announcement_period_length, &protocol_params::leios_announcement_period_length, uint32_t { 100 }, true);
            check(&param_update_t::leios_vote_period_length, &protocol_params::leios_vote_period_length, uint32_t { 101 }, true);
            check(&param_update_t::leios_diffusion_period_length, &protocol_params::leios_diffusion_period_length, uint32_t { 102 }, true);
            check(&param_update_t::leios_committee_size, &protocol_params::leios_committee_size, uint16_t { 103 }, true);
            check(&param_update_t::leios_quorum_stake_threshold, &protocol_params::leios_quorum_stake_threshold, rational_u64 { 1, 2 }, true);
            check(&param_update_t::max_endorser_block_references_size, &protocol_params::max_endorser_block_references_size, uint32_t { 105 }, true);
            check(&param_update_t::max_endorser_block_txs_size, &protocol_params::max_endorser_block_txs_size, uint32_t { 106 }, true);
            check(&param_update_t::max_endorser_block_ex_units, &protocol_params::max_endorser_block_ex_units, ex_units { 123, 456 }, true);
            check(&param_update_t::max_ref_script_size_per_endorser_block, &protocol_params::max_ref_script_size_per_endorser_block, uint32_t { 108 }, true);
            check(&param_update_t::peras_min_candidate_block_age, &protocol_params::peras_min_candidate_block_age, uint32_t { 109 }, true);
            check(&param_update_t::peras_healing_factor, &protocol_params::peras_healing_factor, rational_u64 { 1, 2 }, true);
            check(&param_update_t::peras_cert_boost, &protocol_params::peras_cert_boost, uint16_t { 111 }, true);
            check(&param_update_t::peras_target_committee_size, &protocol_params::peras_target_committee_size, uint16_t { 112 }, true);
            check(&param_update_t::peras_bootstrap_round, &protocol_params::peras_bootstrap_round, nil_optional_t<uint32_t> {}, true);
            check(&param_update_t::peras_quorum_threshold_safety_margin, &protocol_params::peras_quorum_threshold_safety_margin, rational_u64 { 1, 2 }, true);
            check(&param_update_t::ref_inputs_cost_per_multi_asset_policy, &protocol_params::ref_inputs_cost_per_multi_asset_policy, uint64_t { 115 }, false);
            check(&param_update_t::ref_inputs_cost_per_datum_byte, &protocol_params::ref_inputs_cost_per_datum_byte, uint64_t { 116 }, false);
            param_update_t update {};
            update.e_max = 0;
            expect(accepted(update, 11));
            expect(!accepted(update, 12));
            update.e_max = 1;
            expect(accepted(update, 12));
            expect(!accepted({}, 12));
        };
        "Musashi epoch-zero initialization survives epoch changes, reload and reset"_test = [] {
            const configs_dir files { configs_dir::network_path("musashi") };
            const cardano::config cfg { files };
            ledger::state state { cfg };
            expect_equal(cfg.initial_protocol_version.major, 12);
            expect_equal(state.eras().size(), 8);
            expect_equal(state.params().protocol_ver.major, 12);
            expect(state.params().plutus_cost_models.at(3).raw_values() == cfg.dijkstra_genesis->plutus_v4_cost_model);
            const auto params = state.params();
            const auto balance = state.utxo_balance();
            state.start_epoch(0);
            state.start_epoch(1);
            expect(state.params() == params);
            const file::tmp snapshot { "musashi-genesis" };
            state.save_zpp(snapshot.path());
            ledger::state restored { cfg, scheduler::get(), ledger::state::init_mode::empty };
            restored.load_zpp(snapshot.path());
            expect(restored == state);
            expect_equal(restored.utxo_balance(), balance);
            restored.clear();
            expect(restored.params() == params);
            expect_equal(restored.eras().size(), 8);
            expect_equal(restored.utxo_balance(), balance);
            expect(throws([&] { restored.to_cbor({}); }));
        };
    };
};
