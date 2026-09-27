/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */
#include <turbo/cardano/ledger/rules/utxow/requirements.hpp>
#include <turbo/cardano/common/mocks.hpp>
#include <turbo/common/test.hpp>
#include <turbo/crypto/blake2b.hpp>

namespace {
    using namespace turbo;
    using namespace cardano;
    using namespace txwit;
}

suite conway_witness_requirements_suite = [] {
    "UTXOW epoch 587 Byron input accepts its ordinary key witness"_test = [] {
        // Mainnet 17191dff...d74004: the Byron address root is the ordinary
        // witness's key hash. There is no bootstrap witness in this transaction.
        const auto raw = uint8_vector::from_hex(
            "84a40081825820700220ba16c6e21077ce4e8ec32334307074f78cfe993011efba6608b943600700"
            "018182583901044db4c5b66dc0bb2255beccfb08aa527f3a4c073e0bfbc12e2ce245"
            "ebdf20c782830ed01f4220fa603fd4f285dab94ab582bbef4fa26ec11a00258960"
            "021a0002bf20031a3b9ac9ff"
            "a100818258204b6fe0ad43b5370c8d31a633acbde36518aa7cc8b4a3fed1930a502ef2988f29"
            "584063a774a9e8e10d04dccc9268459724d4d47fae783c92774618b262bb93bbacad"
            "884f039b97bdf35d068fe11350400e0873011ec423341b016d99858163e73f03f5f6");
        const auto input_raw = uint8_vector::from_hex(
            "82d818582183581c044db4c5b66dc0bb2255beccfb08aa527f3a4c073e0bfbc12e2ce245a0001a79628f8f");
        const address input { input_raw };
        const auto hash = key_hash::from_hex("044db4c5b66dc0bb2255beccfb08aa527f3a4c073e0bfbc12e2ce245");
        storage::block_info meta {};
        meta.era = 7;
        meta.slot = 168437465;
        mocks::block block { 0, meta, turbo::cardano::config::get() };
        auto parsed = cbor::zero2::parse(raw);
        auto &items = parsed.get().array();
        conway::tx tx { block, 0, items.read() };
        tx.parse_witnesses(items.read());
        expect(tx.hash() == tx_hash::from_hex("17191dff62e42683baab9988c614658edb2859d194d007e3b9c54c3d94d74004"));
        expect(input.is_byron());
        expect(input.byron().root() == hash);
        signer_set verified {};
        expect(tx.witnesses_ok_vkey(verified).vkey == 1_u);
        expect(verified.contains(hash));
        flat_set<required_signer_t> supplied {};
        tx.foreach_witness_shelley_vkey([&](const auto &w) {
            supplied.emplace(vkey_signer_t { crypto::blake2b::digest<key_hash>(w.vkey) });
        });
        tx.foreach_witness_shelley_bootstrap([&](const auto &) { expect(false); });
        const flat_set<required_signer_t> needed { required_signer_t { redeemer_tag::spend, input } };
        const auto no_native = [](const auto &) { return false; };
        expect(missing_required_witness(needed, supplied, no_native) == nullptr);
        cardano::ledger::rules::credential_requirements credentials {};
        credentials.observe_payment(input);
        expect(credentials.keys.size() == 1_u);
        expect(credentials.keys.contains(hash));
        expect(has_key_witness(supplied, hash));
        supplied.clear();
        supplied.emplace(vkey_signer_t { key_hash {} });
        expect(missing_required_witness(needed, supplied, no_native) != nullptr);
        expect(!has_key_witness(supplied, hash));
    };

    "UTXOW key requirements accept bootstrap roots but not scripts"_test = [] {
        const auto hash = key_hash::from_hex("044db4c5b66dc0bb2255beccfb08aa527f3a4c073e0bfbc12e2ce245");
        const flat_set<required_signer_t> needed { required_signer_t { vkey_signer_t { hash } } };
        flat_set<required_signer_t> supplied { required_signer_t { bootstrap_signer_t { hash } } };
        expect(has_key_witness(supplied, hash));
        expect(missing_required_witness(needed, supplied, [](const auto &) { return false; }) == nullptr);
        supplied.clear();
        supplied.emplace(script_signer_t { hash, redeemer_tag::spend });
        expect(!has_key_witness(supplied, hash));
        expect(missing_required_witness(needed, supplied, [](const auto &) { return true; }) != nullptr);
    };

    "UTXOW bootstrap signatures do not supply native script keys"_test = [] {
        storage::block_info meta {};
        meta.era = 7;
        mocks::block block { 0, meta, turbo::cardano::config::get() };
        cbor::encoder body {}, witnesses {};
        body.map(3).uint(0).array(1).array(2).bytes(tx_hash {}).uint(0);
        body.uint(1).array(0).uint(2).uint(0);
        const auto [sk, vk] = crypto::ed25519::create_from_seed(crypto::ed25519::seed {});
        const auto hash = crypto::blake2b::digest<tx_hash>(body.cbor());
        const auto sig = crypto::ed25519::sign(hash, sk);
        const auto attrs = uint8_vector::from_hex("a0");
        witnesses.map(1).uint(2).array(1).array(4)
            .bytes(vk).bytes(sig).bytes(crypto::ed25519::vkey {}).bytes(attrs);
        auto body_value = cbor::zero2::parse(body.cbor());
        auto witness_value = cbor::zero2::parse(witnesses.cbor());
        conway::tx tx { block, 0, body_value.get() };
        tx.parse_witnesses(witness_value.get());
        signer_set native_keys {};
        expect(tx.witnesses_ok_vkey(native_keys).vkey == 1_u);
        expect(native_keys.empty());
    };

    "UTXOW input payment credentials retain the spending purpose"_test = [] {
        uint8_vector raw(29, 0);
        raw[0] = 0x61; // enterprise key address, mainnet
        raw[28] = 1;
        const required_signer_t key { redeemer_tag::spend, address { raw } };
        expect(std::holds_alternative<vkey_signer_t>(key.val));
        raw[0] = 0x71; // enterprise script address
        const required_signer_t script { redeemer_tag::spend, address { raw } };
        expect(std::holds_alternative<script_signer_t>(script.val));
        expect(std::get<script_signer_t>(script.val).tag == redeemer_tag::spend);
        // An input credential is distinct from an unrelated supplied key.
        flat_set<required_signer_t> supplied {}, needed { key };
        const auto no_native = [](const auto &) { return false; };
        expect(missing_required_witness(needed, supplied, no_native) != nullptr);
        supplied.emplace(key);
        expect(missing_required_witness(needed, supplied, no_native) == nullptr);
        needed.emplace(script);
        expect(missing_required_witness(needed, supplied, no_native) != nullptr);
        expect(missing_required_witness(needed, supplied, [](const auto &) { return true; }) == nullptr);
    };

    "UTXOW zero withdrawals and all voter roles require authorization"_test = [] {
        credential_t cred {};
        auto key = withdrawal_signer(cred);
        expect(std::holds_alternative<vkey_signer_t>(key.val));
        cred.script = true;
        auto script = withdrawal_signer(cred);
        flat_set<required_signer_t> needed { script }, supplied {};
        expect(missing_required_witness(needed, supplied, [](const auto &) { return false; }) != nullptr);
        expect(std::get<script_signer_t>(script.val).tag == redeemer_tag::reward);
        for (const auto type: { voter_t::const_comm_key, voter_t::drep_key, voter_t::pool_key })
            expect(std::holds_alternative<vkey_signer_t>(voter_signer({ type, {} }).val));
        for (const auto type: { voter_t::const_comm_script, voter_t::drep_script })
            expect(std::get<script_signer_t>(voter_signer({ type, {} }).val).tag == redeemer_tag::vote);
    };

    "UTXOW proposal policy is required without an explicit signer field"_test = [] {
        flat_set<required_signer_t> required {};
        gov_action_t action {};
        gov_action_t::treasury_withdrawals_t wdrl {};
        wdrl.policy_id.emplace(script_hash {});
        action.val = wdrl;
        add_proposal_signer(required, action);
        const flat_set<required_signer_t> no_witnesses {};
        expect(missing_required_witness(required, no_witnesses, [](const auto &) { return false; }) != nullptr);
        expect(required.contains(required_signer_t { script_signer_t { script_hash {}, redeemer_tag::propose } }));
        required.clear();
        action.val = gov_action_t::info_action_t {};
        add_proposal_signer(required, action);
        expect(required.empty());
    };

    "UTXOW exact script domain rejects missing extra and duplicate-reference witnesses"_test = [] {
        const script_hash a {};
        script_hash b {};
        b[0] = 1;
        flat_set<script_hash> needed { a }, refs {}, supplied {};
        expect(!exact_script_witnesses(needed, refs, supplied));
        supplied.emplace(a);
        expect(exact_script_witnesses(needed, refs, supplied));
        supplied.emplace(b);
        expect(!exact_script_witnesses(needed, refs, supplied));
        supplied.erase(b);
        refs.emplace(a);
        expect(!exact_script_witnesses(needed, refs, supplied));
        supplied.clear();
        expect(exact_script_witnesses(needed, refs, supplied));
        refs.emplace(b); // an unused reference is allowed
        expect(exact_script_witnesses(needed, refs, supplied));
    };
};
