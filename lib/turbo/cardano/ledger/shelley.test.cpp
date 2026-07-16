/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/ledger/shelley.hpp>
#include <turbo/cardano/ledger/state.hpp>
#include <turbo/cbor/compare.hpp>
#include <turbo/common/test.hpp>
#include <turbo/json.hpp>

namespace {
    using namespace turbo;
    using namespace cardano;
    using namespace ledger;
}

suite cardano_ledger_shelley_suite = [] {
    using boost::ext::ut::v2_1_0::nothrow;
    "cardano::ledger::shelley"_test = [] {
        //auto &sched = scheduler::get();
        "max_epoch_slot"_test = [] {
            ledger::shelley::vrf_state st {};
            expect_equal(432000 - 129600, st.max_epoch_slot());
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