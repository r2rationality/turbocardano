/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/ledger/rules/utxow/reference-native.hpp>
#include <turbo/common/test.hpp>

namespace {
    using namespace turbo;
    using namespace cardano;
    using ledger::conway::rules::utxow::validate_reference_native;
}

suite cardano_ledger_reference_native_suite = [] {
    "Conway native timelocks use transaction bounds including expiry equality"_test = [] {
        const auto start = uint8_vector::from_hex("82041864");
        const auto expire = uint8_vector::from_hex("82051864");
        const signer_set signers {};
        expect(native_script::validate(cbor::zero2::parse(start).get(), native_script::validity_interval {}, signers).has_value());
        expect(!native_script::validate(cbor::zero2::parse(start).get(), native_script::validity_interval { 100, {} }, signers));
        expect(!native_script::validate(cbor::zero2::parse(expire).get(), native_script::validity_interval { {}, 100 }, signers));
        expect(native_script::validate(cbor::zero2::parse(expire).get(), native_script::validity_interval { {}, 101 }, signers).has_value());
        std::optional<script_info> referenced { script_info { script_type::native, start } };
        std::optional<signer_set> cached {};
        expect(throws([&] {
            validate_reference_native(referenced, cached, native_script::validity_interval {}, tx_hash {}, [](auto &) {});
        }));
        expect(referenced.has_value());
    };
    "UTXOW checks every distinct reference native script"_test = [] {
        std::optional<signer_set> vkeys {};
        size_t collections = 0;
        const auto collect = [&](signer_set &) { ++collections; };
        // RequireAllOf [] succeeds; RequireAnyOf [] fails with the same keys.
        std::optional<script_info> valid { script_info { script_type::native, uint8_vector::from_hex("820180") } };
        std::optional<script_info> invalid { script_info { script_type::native, uint8_vector::from_hex("820280") } };
        const tx_hash tx_id {};

        validate_reference_native(valid, vkeys, 100, tx_id, collect);
        expect(!valid.has_value());
        expect(collections == 1U);

        bool rejected = false;
        try {
            validate_reference_native(invalid, vkeys, 100, tx_id, collect);
        } catch (const error &) {
            rejected = true;
        }
        expect(rejected);
        expect(invalid.has_value());
        expect(collections == 1U);
    };

    "UTXOW reuses a successful reference script for another purpose"_test = [] {
        std::optional<signer_set> vkeys {};
        size_t collections = 0;
        const auto collect = [&](signer_set &) { ++collections; };
        std::optional<script_info> script { script_info { script_type::native, uint8_vector::from_hex("820180") } };
        const tx_hash tx_id {};

        validate_reference_native(script, vkeys, 100, tx_id, collect);
        validate_reference_native(script, vkeys, 100, tx_id, collect);
        expect(!script.has_value());
        expect(collections == 1U);
    };
};
