/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/ledger/shelley.hpp>
#include <turbo/cardano/ledger/state.hpp>
#include <turbo/cbor/compare.hpp>
#include <turbo/common/scheduler.hpp>
#include <turbo/common/test.hpp>
#include <turbo/json.hpp>

namespace {
    using namespace turbo;
    using namespace cardano;
    using namespace ledger;

    struct reward_test_state: ledger::shelley::state {
        using state::_accounts;
        using state::_epoch_slot;
        using state::_potential_rewards;
        using state::_reward_pulsing_snapshot;
        using state::_rewards_ready;
        using state::_ensure_reward_pulsing_snapshot;
        using state::_decode_possible_update;

        size_t computations = 0;

        reward_test_state(const cardano::config &cfg, const uint64_t major, scheduler &sched=scheduler::get()):
            state { cfg, sched, ledger::shelley::state_init_mode::empty }
        {
            _apply_shelley_params(_params);
            _params.protocol_ver = { major, 0 };
            _params.decentralization = { 1, 1 };
            _params.n_opt = 1;
            _params_prev = _params;
            _epoch = 1;
            _pulsing_snapshot_slot = slot::from_epoch(_epoch, cfg) + cfg.shelley_randomness_stabilization_window;
            _reserves = cfg.shelley_max_lovelace_supply - 10'000;
            const pool_hash pool {};
            pool_params params {};
            params.margin = { 0, 1 };
            params.reward_id[0] = 0xE0 | cfg.shelley_network_id;
            _go.pool_params.emplace(pool, params);
            _go.pool_dist.create(pool);
            _go.pool_dist.add(pool, 10'000);
            _blocks_before.add(pool, 1);
            const stake_ident member { key_hash::from_hex("01000000000000000000000000000000000000000000000000000000"), false };
            auto &acc = _accounts[member];
            acc.ptr.emplace(0, 0, 0);
            acc.go_deleg = pool;
            acc.go_stake = 10'000;
        }

        void _compute_rewards() override
        {
            ++computations;
            state::_compute_rewards();
        }
    };
}

suite cardano_ledger_shelley_suite = [] {
    using boost::ext::ut::v2_1_0::nothrow;
    "cardano::ledger::shelley"_test = [] {
        "early rewards are retained across reload and normal completion"_test = [] {
            cardano::config cfg { cardano::config::get() };
            cfg.shelley_start_epoch(0);
            for (const uint64_t major: { 2, 8 }) {
                reward_test_state early { cfg, major };
                reward_test_state normal { cfg, major };
                early._epoch_slot = cfg.shelley_randomness_stabilization_window;
                early.complete_pulsers();
                expect(!early._rewards_ready);
                expect_equal(early.computations, 0);
                early._epoch_slot++;
                early.complete_pulsers();
                expect(early._rewards_ready);
                expect(!early._potential_rewards.empty());
                expect(early._potential_rewards.at(early._accounts.begin()->first).begin()->amount > 0);
                expect_equal(early._accounts.begin()->second.reward, 0);
                expect_equal(early.computations, 1);
                early.complete_pulsers();
                expect_equal(early.computations, 1);

                const file::tmp path { "shelley-early-rewards" };
                zpp_encoder enc {};
                early.to_zpp(enc);
                enc.run(scheduler::get(), "encode-early-rewards");
                enc.save(path.path(), true);
                reward_test_state restored { cfg, major };
                parallel_decoder dec { path.path() };
                restored.from_zpp(dec);
                dec.run(scheduler::get(), "decode-early-rewards");
                restored.complete_pulsers();
                expect_equal(restored.computations, 0);
                for (auto *st: { &early, &normal, &restored }) {
                    st->_epoch_slot = cfg.shelley_rewards_ready_slot;
                    st->run_pulser_if_ready();
                }
                expect_equal(early.computations, 1);
                expect_equal(normal.computations, 1);
                expect_equal(restored.computations, 0);
                expect(early == normal);
                expect(restored == normal);
                for (auto *st: { &early, &normal, &restored })
                    st->start_epoch(2);
                expect(early == normal);
                expect(restored == normal);
                expect(early._accounts.begin()->second.reward > 0);
            }
        };
        "an empty reward eligibility snapshot stays frozen"_test = [] {
            cardano::config cfg { cardano::config::get() };
            cfg.shelley_start_epoch(0);
            reward_test_state st { cfg, 2 };
            st._accounts.begin()->second.ptr.reset();
            const auto start = slot::from_epoch(1, cfg) + cfg.shelley_randomness_stabilization_window + 1;
            st._ensure_reward_pulsing_snapshot(start);
            expect(st._reward_pulsing_snapshot.empty());
            st._accounts.begin()->second.ptr.emplace(start + 1, 0, 0);
            st._epoch_slot = cfg.shelley_randomness_stabilization_window + 2;
            st.complete_pulsers();
            expect(st._rewards_ready);
            expect(st._reward_pulsing_snapshot.empty());
            expect(st._potential_rewards.empty());
        };
        "failed reward completion can be retried"_test = [] {
            cardano::config cfg { cardano::config::get() };
            cfg.shelley_start_epoch(0);
            scheduler sched { 4 };
            reward_test_state st { cfg, 8, sched };
            st._epoch_slot = cfg.shelley_randomness_stabilization_window + 1;
            const auto pool = *st._accounts.begin()->second.go_deleg;
            st._accounts.begin()->second.go_deleg->at(0) = 1;
            expect(throws([&] { st.complete_pulsers(); }));
            expect(!sched.process_ok(false));
            expect(!st._rewards_ready);
            expect(st._potential_rewards.empty());
            st._accounts.begin()->second.go_deleg = pool;
            st.complete_pulsers();
            expect(sched.process_ok(false));
            expect(st._rewards_ready);
            expect(!st._potential_rewards.empty());
        };
        "an empty completed reward update remains complete after CBOR decoding"_test = [] {
            cardano::config cfg { cardano::config::get() };
            cfg.shelley_start_epoch(0);
            reward_test_state st { cfg, 8 };
            st._epoch_slot = cfg.shelley_randomness_stabilization_window + 1;
            cbor::encoder enc {};
            enc.array(1).array(2).uint(1).array(5)
                .uint(10).uint(20).map(0).uint(0).array(2).map(0).uint(30);
            auto parsed = cbor::zero2::parse(enc.cbor());
            st._decode_possible_update(parsed.get());
            expect(st._rewards_ready);
            expect(st._potential_rewards.empty());
            st.complete_pulsers();
            expect_equal(st.computations, 0);
        };
        "max_epoch_slot"_test = [] {
            ledger::shelley::vrf_state st {};
            expect_equal(432000 - 129600, st.max_epoch_slot());
        };
        "genesis delegation activation"_test = [] {
            state st {};
            const auto genesis = st.shelley_delegs().begin()->first;
            const auto original = st.shelley_delegs().at(genesis);
            const auto delegate = pool_hash::from_hex(
                "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
            const auto vrf = vrf_vkey::from_hex(
                "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
            constexpr uint64_t cert_slot = 10;
            const auto activation = cert_slot + cardano::config::get().shelley_stability_window;

            st.process_cert(cert_t { genesis_deleg_cert { genesis, delegate, vrf } },
                cert_loc_t { cert_slot, 0, 0 });
            expect_equal(original, st.shelley_delegs().at(genesis));
            expect_equal(original, st.shelley_delegs_schedule().at(activation - 1).at(genesis));

            st.process_cert(cert_t { instant_reward_cert {} }, cert_loc_t { activation, 0, 0 });
            expect_equal(shelley_delegate { delegate, vrf }, st.shelley_delegs().at(genesis));
            expect_equal(shelley_delegate { delegate, vrf },
                st.shelley_delegs_schedule().at(activation).at(genesis));

            file::tmp snapshot { "shelley-genesis-delegation" };
            st.save_zpp(snapshot.path());
            state restored { cardano::config::get(), scheduler::get(), state::init_mode::empty };
            restored.load_zpp(snapshot.path());
            expect(st.shelley_delegs_schedule() == restored.shelley_delegs_schedule());
        };
        "genesis delegation uniqueness"_test = [] {
            state st {};
            auto genesis = st.shelley_delegs().begin();
            const auto other = std::next(genesis);
            expect(throws([&] {
                st.process_cert(cert_t { genesis_deleg_cert {
                    genesis->first, other->second.delegate, other->second.vrf
                } }, cert_loc_t { 10, 0, 0 });
            }));
        };
        /*"cbor load/save"_test = [&] {
            state st {};
            st.process_updates()
            st.start_epoch(1U);
            const point tip {{}, 22, 33, 0};
            const auto ser = st.to_cbor(tip);
            const auto res_bytes = ser.flat();
            file::write(install_path("tmp/test-cardano-ledger-shelley/ledger-out.cbor"), res_bytes);
            const auto act_bytes = file::read(install_path("data/shelley/ledger-0.cbor"));
            expect_equal(res_bytes.size(), res_bytes.size());
            const auto act_tip = st.deserialize_node(act_bytes);
            expect_equal(tip, act_tip);
        };*/
        "epoch_nonce default"_test = [] {
            const ledger::shelley::vrf_state vrf_state {};
            expect_equal(
                vrf_state.nonce_epoch(),
                vrf_nonce::from_hex("1a3be38bcbb7911969283716ad7aa550250226b76a61fc51cc9a9a35d9276d81")
            );
            expect_equal(vrf_state.uc_nonce(), vrf_nonce::from_hex("81e47a19e6b29b0a65b9591762ce5143ed30d0261e5d24a3201752506b20f15c"));
            expect_equal(vrf_state.uc_leader(), vrf_nonce::from_hex("12dd0a6a7d0e222a97926da03adb5a7768d31cc7c5c2bd6828e14a7d25fa3a60"));
        };
        "epoch_nonce manual"_test = [] {
            configs_mock::map_type cfg {};
            cfg.emplace("byron-genesis", turbo::json::load("./etc/mainnet/byron-genesis.json").as_object());
            {
                auto shelley_genesis = turbo::json::load("./etc/mainnet/shelley-genesis.json").as_object();
                shelley_genesis.insert_or_assign("startTime", 1234567890);
                cfg.emplace("shelley-genesis", std::move(shelley_genesis));
            }
            cfg.emplace("alonzo-genesis", turbo::json::load("./etc/mainnet/alonzo-genesis.json").as_object());
            cfg.emplace("conway-genesis", turbo::json::load("./etc/mainnet/conway-genesis.json").as_object());
            cfg.emplace("config", turbo::json::object {
                { "ByronGenesisFile", "byron-genesis" },
                { "ByronGenesisHash", fmt::format("{}", crypto::blake2b::digest<cardano::block_hash>(turbo::json::serialize_canon(cfg.at("byron-genesis").json()))) },
                { "ShelleyGenesisFile", "shelley-genesis" },
                { "ShelleyGenesisHash", fmt::format("{}", crypto::blake2b::digest<cardano::block_hash>(cfg.at("shelley-genesis").bytes())) },
                { "AlonzoGenesisFile", "alonzo-genesis" },
                { "AlonzoGenesisHash", fmt::format("{}", crypto::blake2b::digest<cardano::block_hash>(cfg.at("alonzo-genesis").bytes())) },
                { "ConwayGenesisFile", "conway-genesis" },
                { "ConwayGenesisHash", fmt::format("{}", crypto::blake2b::digest<cardano::block_hash>(cfg.at("conway-genesis").bytes())) }
            });
            cardano::config c_cfg { configs_mock { std::move(cfg) } };
            const ledger::shelley::vrf_state vrf_state { c_cfg };
            expect_equal(vrf_state.nonce_epoch(), vrf_nonce::from_hex("5403C5AA8CB9B076BB54809BF7E44333EE1B8B662C80D8EEB2C9414E631CD006"));
        };
    };
};
