/* Copyright (c) 2026 R2 Rationality OÜ. License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */
#include "conway-validation.hpp"
#include <map>
#include <turbo/cardano/common/mocks.hpp>
#include <turbo/cardano/ledger/rules/utxo/collateral.hpp>
#include <turbo/cardano/ledger/rules/utxos/phase2.hpp>
#include <turbo/common/test.hpp>
#include <turbo/crypto/blake2b.hpp>

namespace {
    using namespace turbo;
    using namespace cardano;
    using namespace cardano::ledger::rules;

    tx_output output(bool script=false, uint64_t coin=1'000'000)
    {
        tx_output out {};
        out.address_raw.resize(29, 0);
        out.address_raw[0] = (script ? 0x70 : 0x60) | turbo::cardano::config::get().shelley_network_id;
        out.coin = coin;
        return out;
    }

    protocol_params parameters()
    {
        protocol_params params {};
        params.max_value_size = 5000;
        params.lovelace_per_utxo_byte = 1;
        params.plutus_cost_models.items.emplace(0, plutus_cost_model { std::vector<int64_t> { 1 } });
        params.plutus_cost_models.items.emplace(1, plutus_cost_model { std::vector<int64_t> { 2 } });
        params.plutus_cost_models.items.emplace(2, plutus_cost_model { std::vector<int64_t> { 3 } });
        return params;
    }

    void commit(conway_validation &checks, const protocol_params &params, script_type language)
    {
        checks.integrity_prefix = uint8_vector::from_hex("A0");
        checks.integrity_hash = script_integrity_hash(checks.integrity_prefix,
            flat_set<script_type> { language }, params.plutus_cost_models, true);
    }
}

suite conway_validation_suite = [] {
    "UTXOW voting pointers count distinct voters including key voters"_test = [] {
        storage::block_info meta {};
        meta.era = 7;
        mocks::block block { 0, meta, turbo::cardano::config::get() };
        cbor::encoder body {}, witnesses {};
        body.map(4).uint(0).array(1).array(2).bytes(tx_hash {}).uint(0);
        body.uint(1).array(0).uint(2).uint(200'000).uint(19).map(2);
        const script_hash script {};
        for (const auto voter_type: { 2U, 3U }) { // key DRep, then script DRep
            body.array(2).uint(voter_type).bytes(script).map(2);
            for (const auto action_index: { 0U, 1U })
                body.array(2).bytes(tx_hash {}).uint(action_index).array(2).uint(1).s_null();
        }
        witnesses.map(0);
        auto body_value = cbor::zero2::parse(body.cbor());
        auto witness_value = cbor::zero2::parse(witnesses.cbor());
        conway::tx tx { block, 0, body_value.get() };
        tx.parse_witnesses(witness_value.get());
        conway_validation checks {};
        checks.collect(tx, {});
        expect(tx.votes().size() == 4_u);
        expect(checks.credentials.keys.size() == 1_u);
        expect(checks.credentials.scripts.size() == 1_u);
        expect(checks.credentials.purposes.size() == 1_u);
        const redeemer_id pointer { redeemer_tag::vote, 1 };
        expect(checks.credentials.purposes.contains(pointer));
        if (const auto it = checks.credentials.purposes.find(pointer); it != checks.credentials.purposes.end())
            expect(it->second == script);
    };

    "UTXOW certificates and zero withdrawals share credential projections"_test = [] {
        storage::block_info meta {};
        meta.era = 7;
        mocks::block block { 0, meta, turbo::cardano::config::get() };
        const script_hash script {};
        uint8_vector reward(29, 0);
        reward[0] = 0xF0 | block.config().shelley_network_id;
        cbor::encoder body {}, witnesses {};
        body.map(5).uint(0).array(1).array(2).bytes(tx_hash {}).uint(0);
        body.uint(1).array(0).uint(2).uint(200'000);
        body.uint(4).array(2);
        for (size_t i = 0; i < 2; ++i)
            body.array(2).uint(1).array(2).uint(1).bytes(script); // equal deregistration certificates
        body.uint(5).map(1).bytes(reward).uint(0);
        witnesses.map(0);
        auto body_value = cbor::zero2::parse(body.cbor());
        auto witness_value = cbor::zero2::parse(witnesses.cbor());
        conway::tx tx { block, 0, body_value.get() };
        tx.parse_witnesses(witness_value.get());
        conway_validation checks {};
        checks.collect(tx, {});
        expect(checks.credentials.keys.empty());
        expect(checks.credentials.scripts.size() == 1_u);
        expect(checks.credentials.purposes.size() == 2_u);
        expect(checks.credentials.purposes.contains({ redeemer_tag::cert, 0 }));
        expect(!checks.credentials.purposes.contains({ redeemer_tag::cert, 1 }));
        expect(checks.credentials.purposes.contains({ redeemer_tag::reward, 0 }));
        checks.credentials.observe_payment(output().addr()); // collateral key: no spending pointer
        expect(checks.credentials.keys.size() == 1_u);
        expect(checks.credentials.purposes.size() == 2_u);
    };

    "UTXOW redeemer domain preserves repeated spending script identity"_test = [] {
        auto params = parameters();
        conway_validation checks {};
        checks.scripts.emplace(script_hash {}, script_type::plutus_v3);
        const auto out = output(true);
        checks.observe_input(out, 0);
        checks.observe_input(out, 1);
        checks.redeemers.emplace(redeemer_id { redeemer_tag::spend, 0 });
        commit(checks, params, script_type::plutus_v3);
        expect(throws([&] { checks.validate(params); }));
        checks.redeemers.emplace(redeemer_id { redeemer_tag::spend, 1 });
        expect(nothrow([&] { checks.validate(params); }));
        checks.redeemers.emplace(redeemer_id { redeemer_tag::spend, 2 });
        expect(throws([&] { checks.validate(params); }));
    };

    "UTXOW missing and unrelated datums fail before script evaluation"_test = [] {
        for (const auto language: { script_type::plutus_v1, script_type::plutus_v2 }) {
            conway_validation checks {};
            checks.scripts.emplace(script_hash {}, language);
            auto out = output(true);
            expect(throws([&] { checks.observe_input(out, 0); }));
            out.datum = datum_option_t { datum_hash {} };
            expect(throws([&] { checks.observe_input(out, 0); }));
            checks.datums.emplace(datum_hash {});
            expect(nothrow([&] { checks.observe_input(out, 0); }));
        }
        conway_validation checks {};
        checks.datums.emplace(datum_hash {});
        expect(throws([&] { checks.validate(parameters()); }));
        conway_validation v3 {};
        v3.scripts.emplace(script_hash {}, script_type::plutus_v3);
        expect(nothrow([&] { v3.observe_input(output(true), 0); }));
        auto hashed = output(true);
        hashed.datum = datum_option_t { datum_hash {} };
        expect(throws([&] { v3.observe_input(hashed, 1); }));
    };

    "UTXOW integrity language views preserve V1 encoding and CBOR ordering"_test = [] {
        const auto params = parameters();
        const auto cache = make_language_views(params.plutus_cost_models);
        // V2: key 1, [2]; V1: bytes(0), bytes(indefinite [1]).
        const auto expected = uint8_vector::from_hex("A20181024100439F01FF");
        expect(cache[3] == expected);
        expect(!script_integrity_hash({}, {}, params.plutus_cost_models, false));
        const auto prefix = uint8_vector::from_hex("A0");
        uint8_vector bytes { prefix };
        bytes.insert(bytes.end(), expected.begin(), expected.end());
        const flat_set<script_type> languages { script_type::plutus_v1, script_type::plutus_v2 };
        expect(script_integrity_hash(prefix, languages, params.plutus_cost_models, true, &cache)
            == std::optional<hash_32> { crypto::blake2b::digest<hash_32>(bytes) });
        conway_validation empty {};
        empty.integrity_hash = hash_32 {};
        expect(throws([&] { empty.validate(params); }));
    };

    "UTXO collateral validates keys value percentage and return independently"_test = [] {
        collateral_balance empty {};
        expect(throws([&] { empty.validate({}, {}, 100, 150); }));
        expect(throws([&] { empty.add(output(true)); }));
        collateral_balance ada {};
        ada.add(output(false, 150));
        expect(ada.validate({}, 150, 100, 150) == 150_u);
        expect(throws([&] { ada.validate({}, {}, 101, 150); }));
        expect(throws([&] { ada.validate({}, 149, 100, 150); }));
        expect(throws([&] { ada.validate(output(false, 151), {}, 0, 150); }));
        expect(throws([&] { ada.validate({}, {}, UINT64_MAX, UINT64_MAX); }));
        collateral_balance tokens {};
        auto input = output(false, 200);
        input.assets[script_hash {}][asset_name_t {}] = 1;
        tokens.add(input);
        expect(throws([&] { tokens.validate({}, {}, 100, 150); }));
        input.coin = 50;
        expect(tokens.validate(input, 150, 100, 150) == 150_u);
        input.assets[script_hash {}][asset_name_t {}] = 2;
        expect(throws([&] { tokens.validate(input, 150, 100, 150); }));
        input.assets.clear();
        expect(throws([&] { tokens.validate(input, 150, 100, 150); }));
        collateral_balance no_tokens {};
        no_tokens.add(output(false, 200));
        input.assets[script_hash {}][asset_name_t {}] = 1;
        expect(throws([&] { no_tokens.validate(input, 150, 100, 150); }));
        collateral_balance overflow {};
        overflow.add(output(false, UINT64_MAX));
        overflow.add(output(false, 1));
        expect(throws([&] { overflow.validate({}, {}, 0, 0); }));
    };

    "UTXOS compares the actual phase2 result and keeps preparation errors fatal"_test = [] {
        std::map<int, int> scripts { {0, 0}, {1, 1} };
        const auto prepare = [](int script) { return script; };
        const auto succeed = [](int) {};
        const auto fail = [](int) { throw error("script failure"); };
        expect(nothrow([&] { validate_phase2_result(scripts, true, prepare, succeed); }));
        expect(throws([&] { validate_phase2_result(scripts, false, prepare, succeed); }));
        expect(nothrow([&] { validate_phase2_result(scripts, false, prepare, fail); }));
        expect(throws([&] { validate_phase2_result(scripts, true, prepare, fail); }));
        const auto bad_prepare = [](int script) {
            if (script == 1)
                throw error("bad context");
            return script;
        };
        expect(throws([&] { validate_phase2_result(scripts, false, bad_prepare, fail); }));
        scripts.clear();
        expect(throws([&] { validate_phase2_result(scripts, false, prepare, succeed); }));
    };

    "UTXO current treasury is checked even without scripts"_test = [] {
        conway_validation checks {};
        checks.treasury = 41;
        expect(nothrow([&] { checks.validate(parameters(), nullptr, 41); }));
        expect(throws([&] { checks.validate(parameters(), nullptr, 42); }));
    };

    "Conway decoding retains sizes and rejects an auxiliary commitment without data"_test = [] {
        storage::block_info meta {};
        meta.era = 7;
        mocks::block block { 0, meta, turbo::cardano::config::get() };
        const auto check = [&](bool add_hash) {
            cbor::encoder body {}, witnesses {};
            const auto out = output();
            body.map(add_hash ? 4 : 3).uint(0).array(1).array(2).bytes(tx_hash {}).uint(0);
            body.uint(1).array(1).array(2).bytes(out.address_raw).uint(out.coin);
            body.uint(2).uint(200'000);
            if (add_hash)
                body.uint(7).bytes(hash_32 {});
            witnesses.map(0);
            auto body_value = cbor::zero2::parse(body.cbor());
            auto witness_value = cbor::zero2::parse(witnesses.cbor());
            conway::tx tx { block, 0, body_value.get() };
            tx.parse_witnesses(witness_value.get());
            conway_validation checks {};
            if (add_hash)
                expect(throws([&] { checks.collect(tx, {}); }));
            else {
                checks.collect(tx, {});
                expect(checks.outputs.max_coins_per_utxo_byte != UINT64_MAX);
                expect(conway_tx_size(tx, {}) == body.cbor().size() + witnesses.cbor().size() + 2);
                const auto auxiliary = uint8_vector::from_hex("A10000");
                expect(conway_tx_size(tx, buffer { auxiliary }) == body.cbor().size() + witnesses.cbor().size() + 4);
                expect(nothrow([&] { checks.validate(parameters()); }));
            }
        };
        check(false);
        check(true);
    };
};
