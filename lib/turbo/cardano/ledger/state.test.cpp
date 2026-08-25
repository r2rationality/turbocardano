/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/ledger/state.hpp>
#include <turbo/common/scheduler.hpp>
#include <turbo/common/test.hpp>

namespace {
    using namespace turbo;
    using namespace cardano::ledger;

    void update_params(state &st, const uint64_t slot, const cardano::param_update &update)
    {
        const cardano::slot slot_obj { slot, cardano::config::get() };
        for (const auto &[deleg_id, meta]: cardano::config::get().shelley_delegates) {
            st.propose_update(slot, { .key_id=deleg_id, .update=update, .epoch=slot_obj.epoch() });
        }
    }
}

suite cardano_ledger_state_suite = [] {
    using boost::ext::ut::v2_1_0::nothrow;
    "cardano::ledger::state"_test = [] {
        "empty"_test = [] {
            state st {};
            expect(st.epoch() == 0_ull);
            expect(st.end_offset() == 0_ull);
            expect(st.pool_stake_dist().empty());
            expect(!st.utxos().empty());

            state snapshot_target {
                cardano::config::get(), scheduler::get(), state::init_mode::empty
            };
            expect(snapshot_target.utxos().empty());
            snapshot_target.clear(state::init_mode::empty);
            expect(snapshot_target.utxos().empty());
            snapshot_target.clear();
            expect(snapshot_target == st);
        };
        "finish_epoch"_test = [] {
            state st {};
            expect_equal(st.params().protocol_ver.major, 0);
            update_params(st, cardano::slot { 1, cardano::config::get() }, { .protocol_ver=cardano::protocol_version { 2, 0 } });
            expect_equal(st.treasury(), 0);
            expect_equal(st.reserves(), 0);
            st.start_epoch();
            st.process_block(20000, 2, 0, 0);
            cardano::config::get().shelley_start_epoch(0);
            expect_equal(st.treasury(), 0);
            expect_equal(st.reserves(), 13887515255000000ULL);
            expect_equal(st.params().protocol_ver.major, 2);
            expect_equal(st.treasury(), 0);
            st.process_block(40000, 2, 400000, 0);
            st.start_epoch();
            expect_equal(st.treasury(), 8332509153000ULL);
            st.start_epoch();
            expect_equal(st.epoch(), 2);
        };
        "clear"_test = [] {
            file::tmp tmp_state { "validator-state-test" };
            state st {};
            update_params(st, cardano::slot { 1, cardano::config::get() }, { .protocol_ver=cardano::protocol_version { 8, 0 } });
            st.start_epoch();
            st.reserves(10'000'000'000'000'000ULL);
            st.start_epoch();
            st.clear();
            state st2 {};
            expect(st == st2);
        };
        "save and load"_test = [] {
            file::tmp tmp_state { "validator-state-test" };
            state st {};
            update_params(st, 1, { .protocol_ver=cardano::protocol_version { 8, 0 } });
            st.start_epoch();
            st.reserves(10'000'000'000'000'000ULL);
            st.start_epoch();
            st.save_zpp(tmp_state.path());
            state st2 {
                cardano::config::get(), scheduler::get(), state::init_mode::empty
            };
            expect(st2.utxos().empty());
            st2.load_zpp(tmp_state.path());
            expect(st == st2);
        };
        "save_node and load_node"_test = [] {
            file::tmp tmp_state { "validator-state-node-test" };
            state st {};
            update_params(st, 1, { .protocol_ver=cardano::protocol_version { 8, 0 } });
            st.start_epoch();
            st.reserves(10'000'000'000'000'000ULL);
            st.start_epoch();
            st.save_node(tmp_state.path(), cardano::point {});
            state st2 {};
            st2.load_node(tmp_state.path());
            expect(st == st2);
        };
        "save_node and load_node pv11"_test = [] {
            file::tmp tmp_state { "validator-state-node-pv11-test" };
            state st {};
            update_params(st, 1, { .protocol_ver=cardano::protocol_version { 11, 0 } });
            st.start_epoch();
            cardano::pool_params params {};
            params.vrf_vkey = cardano::vrf_vkey::from_hex(
                "000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F");
            params.reward_id.at(0) = 0xE0 | cardano::config::get().shelley_network_id;
            const auto pool_id = cardano::pool_hash::from_hex(
                "000102030405060708090A0B0C0D0E0F101112131415161718191A1B");
            st.register_pool({ pool_id, params });
            params.cost = 1;
            st.register_pool({ pool_id, params });
            st.save_node(tmp_state.path(), cardano::point {});
            state st2 {};
            st2.load_node(tmp_state.path());
            expect(st == st2);
        };
        "register_pool"_test = [] {
            state st {};
            update_params(st, 1, { .protocol_ver=cardano::protocol_version { 8, 0 } });
            st.start_epoch();
            const auto pool_id = cardano::key_hash::from_hex("00000000000000000000000000000000000000000000000000000000");
            cardano::pool_params pp1 {};
            pp1.cost = 200'000'000;
            st.register_pool({ pool_id, pp1 });
            expect(st.pool_params().contains(pool_id));
            expect(st.pool_params().at(pool_id).params.cost == 200'000'000);
            expect(st.pool_params_future().empty());
            cardano::pool_params pp2 {};
            pp2.cost = 300'000'000;
            st.register_pool({ pool_id, pp2 });
            expect(!st.pool_params_future().empty());
            expect(st.pool_params().at(pool_id).params.cost == 200'000'000);
            expect(st.pool_params_future().at(pool_id).params.cost == 300'000'000);
            expect(st.pool_params_mark().empty());
            st.start_epoch();
            expect(st.pool_params().at(pool_id).params.cost == 300'000'000);
            expect(st.pool_params_mark().at(pool_id).params.cost == 200'000'000);
            expect(st.pool_params_future().empty());
        };
        "track_eras"_test = [] {
            state st {};
            expect(nothrow([&] { st.track_era(0, 22); }));
            expect(st.eras().empty());
            expect(nothrow([&] { st.track_era(1, 22); }));
            expect(throws([&] { st.track_era(1, 20); }));
            expect(nothrow([&] { st.track_era(1, 40); }));
            expect(nothrow([&] { st.track_era(2, 100); }));
            expect(throws([&] { st.track_era(2, 99); }));
            expect(throws([&] { st.track_era(3, 99); }));
            expect(nothrow([&] { st.track_era(3, 432101); }));
            expect(throws([&] { st.track_era(2, 102); }));
            const auto eras = st.eras();
            expect(eras.size() == 3) << eras.size();
            expect(eras.at(0) == 22) << eras.at(0);
            expect(eras.at(1) == 100) << eras.at(1);
            expect(eras.at(2) == 432100) << eras.at(2);
        };
    };
};
