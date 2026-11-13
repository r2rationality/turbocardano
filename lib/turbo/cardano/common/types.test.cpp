/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/config.hpp>
#include <turbo/cardano/common/types.hpp>
#include <turbo/common/test.hpp>
#include <turbo/zpp.hpp>

namespace {
    using namespace turbo;
    using namespace turbo::cardano;
}

suite cardano_type_test = [] {
    "cardano::type"_test = [] {
        "execution prices accept both genesis field-name pairs"_test = [] {
            const json::value legacy = json::object {
                {"prMem", json::object { {"numerator", 577}, {"denominator", 10000} }},
                {"prSteps", json::object { {"numerator", 721}, {"denominator", 10000000} }}
            };
            const json::value current = json::object { {"priceMemory", 0.0577}, {"priceSteps", 0.0000721} };
            for (const auto &prices: {legacy, current}) {
                const auto parsed = ex_unit_prices::from_json(prices);
                expect(parsed.mem == rational_u64 { 577, 10000 });
                expect(parsed.steps == rational_u64 { 721, 10000000 });
                for (const auto &[name, value]: prices.as_object()) {
                    const json::value incomplete = json::object { {name, value} };
                    expect(throws([&] { ex_unit_prices::from_json(incomplete); }));
                }
            }
        };
        "TXO references retain their serialized field layout"_test = [] {
            const tx_out_ref ref {
                tx_hash::from_hex("000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F"), 0x1234
            };
            // Legacy aggregate serialization encodes each field in declaration order.
            // Use the hash's existing encoding, including its container framing.
            auto expected = turbo::zpp::serialize(ref.hash);
            expected << turbo::zpp::serialize(uint16_t { 0x1234 });
            expect(turbo::zpp::serialize(ref) == expected);
            expect(turbo::zpp::deserialize<tx_out_ref>(expected) == ref);
        };
        "Conway protocol parameters survive snapshots and affect equality"_test = [] {
            const protocol_params defaults {};
            const auto check = [&](const auto member, const auto &value) {
                auto params = defaults;
                params.*member = value;
                expect(params != defaults);
                const auto bytes = turbo::zpp::serialize(params);
                const auto restored = turbo::zpp::deserialize<protocol_params>(bytes);
                // Check the field directly so an equality omission cannot hide data loss.
                expect(restored.*member == value);
                expect(restored == params);
            };
            auto pool_thresholds = defaults.pool_voting_thresholds;
            pool_thresholds.committee_normal = { 3, 5 };
            check(&protocol_params::pool_voting_thresholds, pool_thresholds);
            auto drep_thresholds = defaults.drep_voting_thresholds;
            drep_thresholds.pp_governance_group = { 4, 5 };
            check(&protocol_params::drep_voting_thresholds, drep_thresholds);
            check(&protocol_params::committee_min_size, uint16_t { 5 });
            check(&protocol_params::committee_max_term_length, uint32_t { 100 });
            check(&protocol_params::gov_action_lifetime, uint32_t { 8 });
            check(&protocol_params::gov_action_deposit, uint64_t { 123 });
            check(&protocol_params::drep_deposit, uint64_t { 456 });
            check(&protocol_params::drep_activity, uint32_t { 30 });
            check(&protocol_params::min_fee_ref_script_cost_per_byte, rational_u64 { 17, 1 });
        };
        "credential ordering and index encoding"_test = [] {
            static_assert(sizeof(credential_t::index_key_type) == 29);
            credential_t script { {}, true }, key { {}, false };
            script.hash.fill(0xff);
            expect(script < key);
            expect(!(key < script));
            expect(script != key);
            expect(script.index_key() < key.index_key());
            for (const bool is_script: { true, false }) {
                credential_t id { {}, is_script };
                for (size_t i = 0; i < id.hash.size(); ++i)
                    id.hash[i] = static_cast<uint8_t>(i * 7);
                const auto bytes = id.index_key();
                expect(bytes[0] == (is_script ? 0 : 1));
                expect(std::equal(id.hash.begin(), id.hash.end(), bytes.begin() + 1));
                expect(credential_t::from_index_key(bytes) == id);
                era_encoder enc { era_t::conway };
                id.to_cbor(enc);
                expect(enc.cbor()[1] == (is_script ? 1 : 0));
                auto parsed = cbor::zero2::parse(enc.cbor());
                expect(credential_t::from_cbor(parsed.get()) == id);
            }
            credential_t::index_key_type invalid {};
            invalid[0] = 2;
            expect(throws([&] { credential_t::from_index_key(invalid); }));
            expect(throws([] { credential_t::from_index_key(buffer {}); }));
        };
        "reward address ordering and equality"_test = [] {
            const auto address = [](const uint8_t header, const uint8_t hash_prefix) {
                byte_array<29> bytes {};
                bytes[0] = header;
                bytes[1] = hash_prefix;
                return reward_id_t { bytes };
            };
            // Hash extremes ensure kind and network take precedence over hash bytes.
            const std::array expected {
                address(0xf0, 0), address(0xf0, 255), address(0xe0, 0),
                address(0xf1, 0)
            };
            map_t<reward_id_t, size_t> accounts {};
            for (size_t i = expected.size(); i-- > 0;)
                accounts.emplace(expected[i], i);
            for (size_t i = 0; i < expected.size(); ++i) {
                expect(accounts.nth_or_die(i).first == expected[i]);
                expect_equal(accounts.at(expected[i]), i);
            }
            // Adjacent pairs isolate hash, kind, and network precedence.
            for (size_t i = 1; i < expected.size(); ++i) {
                const auto &a = expected[i - 1];
                const auto &b = expected[i];
                expect(a < b);
                expect(b > a);
                expect(a <= b);
                expect(b >= a);
                expect(a != b);
                expect((a <=> b) == std::strong_ordering::less);
                expect((b <=> a) == std::strong_ordering::greater);
            }
            const auto same = expected[0];
            expect(same == expected[0]);
            expect(!(same < expected[0]));
            expect(!(same > expected[0]));
            expect(same <= expected[0]);
            expect(same >= expected[0]);
            expect((same <=> expected[0]) == std::strong_ordering::equal);
            const credential_t script { expected[0].hash(), true };
            expect(expected[0] == script);
            expect(expected[3] == script); // Credential comparison ignores network.
            expect(expected[0] != expected[3]); // Address equality includes network.
            expect(!(expected[1] == script)); // Different hash.
            expect(!(expected[2] == script)); // Same hash, different kind.
        };
        "protocol point equality"_test = [] {
            const optional_point2 origin {};
            const optional_point2 zero { point2 {} };
            expect_equal(origin, optional_point2 {});
            expect_equal(zero, optional_point2 { point2 {} });
            expect(origin != zero);
            expect(zero != optional_point2 { 1, block_hash {} });
        };
        "DRep order differs from credential order"_test = [] {
            credential_t script { {}, true }, key { {}, false };
            key.hash.fill(0xff);
            const drep_t key_drep { key }, script_drep { script };
            const drep_t abstain { drep_t::abstain_t {} };
            const drep_t no_confidence { drep_t::no_confidence_t {} };
            expect(script < key);
            expect(key_drep < script_drep);
            expect(!(script_drep < key_drep));
            expect(script_drep < abstain);
            expect(abstain < no_confidence);
        };
        "protocol tips distinguish origin from a concrete zero point"_test = [] {
            const auto origin_bytes = uint8_vector::from_hex("828000"); // [[], 0]
            auto parsed = cbor::zero2::parse(origin_bytes);
            const auto origin = optional_point3::from_cbor(parsed.get());
            expect(!origin);
            expect_equal(origin.height, 0);
            expect(throws([&] { static_cast<void>(static_cast<point3>(origin)); }));
            cbor::encoder encoded_origin;
            origin.to_cbor(encoded_origin);
            expect_equal(encoded_origin.cbor(), origin_bytes);
            auto concrete_origin = cbor::zero2::parse(origin_bytes);
            expect(throws([&] { static_cast<void>(point3::from_cbor(concrete_origin.get())); }));

            const point3 zero {};
            cbor::encoder encoded_zero;
            zero.to_cbor(encoded_zero);
            auto parsed_zero = cbor::zero2::parse(encoded_zero.cbor());
            const auto tip = optional_point3::from_cbor(parsed_zero.get());
            expect(fatal(tip.has_value()));
            expect_equal(*tip, static_cast<const point2 &>(zero));
            expect_equal(tip.height, 0);
            expect(tip != origin);
            cbor::encoder encoded_tip;
            tip.to_cbor(encoded_tip);
            expect_equal(encoded_tip.cbor(), encoded_zero.cbor());

            auto higher = tip;
            higher.height = 1;
            expect(higher != tip);
            expect_equal(static_cast<point3>(higher).height, higher.height);
            const std::optional<point> stored { point { zero.hash, zero.slot, higher.height, 123 } };
            expect_equal(optional_point3 { stored }, higher);
            expect_equal(optional_point3 { *stored }, higher);
            expect_equal(optional_point3 { std::optional<point3> { static_cast<point3>(higher) } }, higher);
            expect_equal(optional_point3 { std::optional<point> {} }, origin);
            cbor::encoder encoded_higher;
            higher.to_cbor(encoded_higher);
            auto parsed_higher = cbor::zero2::parse(encoded_higher.cbor());
            expect_equal(optional_point3::from_cbor(parsed_higher.get()), higher);
        };
        "protocol tip origin ignores the encoded height"_test = [] {
            const auto bytes = uint8_vector::from_hex("8280182A"); // [[], 42]
            auto parsed = cbor::zero2::parse(bytes);
            const auto tip = optional_point3::from_cbor(parsed.get());
            expect(!tip);
            expect_equal(tip.height, 0);
            cbor::encoder encoded;
            tip.to_cbor(encoded);
            expect_equal(encoded.cbor(), uint8_vector::from_hex("828000"));
        };
        "protocol tip rejects malformed array shapes"_test = [] {
            for (const auto hex: { "8180", "83800000", "82810000", "9F8000FF", "829FFF00" }) {
                const auto bytes = uint8_vector::from_hex(hex);
                expect(throws([&] {
                    auto parsed = cbor::zero2::parse(bytes);
                    static_cast<void>(optional_point3::from_cbor(parsed.get()));
                }));
            }
        };
        "address_buf"_test = [] {
            address_buf addr1 { "0xDEADBEAF" };
            expect(addr1.size() == 4_u);
            expect(addr1[0] == 0xDE);
            expect(addr1[1] == 0xAD);
            expect(addr1[2] == 0xBE);
            expect(addr1[3] == 0xAF);

            uint8_t exp[] = {
                0xf1, 0xc3, 0x7b, 0x1b, 0x5d, 0xc0, 0x66, 0x9f, 0x1d, 0x3c, 0x61, 0xa6, 0xfd, 0xdb, 0x2e, 0x8f,
                0xde, 0x96, 0xbe, 0x87, 0xb8, 0x81, 0xc6, 0x0b, 0xce, 0x8e, 0x8d, 0x54, 0x2f
            };
            address_buf addr2 { "stake178phkx6acpnf78fuvxn0mkew3l0fd058hzquvz7w36x4gtcccycj5" };
            expect(addr2.size() == sizeof(exp));
            expect(memcmp(addr2.data(), exp, sizeof(exp)) == 0_i);
        };
        "address pointer"_test = [] {
            auto buf = uint8_vector::from_hex("4186880a8bb19ec8742db9076795c5107f7ffc65a889e7b0980ffeaca20c0c0c");
            cardano::address addr { buf };
            expect(addr.has_pay_id());
            expect(addr.pay_id() == cardano::pay_ident { cardano::key_hash::from_hex("86880a8bb19ec8742db9076795c5107f7ffc65a889e7b0980ffeaca2") });
            expect(addr.has_pointer());
            auto ptr = addr.pointer();
            expect(ptr.slot == 12_u);
            expect(ptr.tx_idx == 12_u);
            expect(ptr.cert_idx == 12_u);
        };
        "address pointer 2"_test = [] {
            auto buf = uint8_vector::from_hex("41fbfce15acccb420982704c9e591f83ab3315c3314a18ecf65346e0858292b3380b00");
            cardano::address addr { buf };
            expect(addr.has_pay_id());
            expect(addr.pay_id() == cardano::pay_ident { cardano::key_hash::from_hex("fbfce15acccb420982704c9e591f83ab3315c3314a18ecf65346e085") });
            expect(addr.has_pointer());
            auto ptr = addr.pointer();
            expect(ptr.slot == 4495800_u);
            expect(ptr.tx_idx == 11_u);
            expect(ptr.cert_idx == 0_u);
        };
        "address pointer extra data"_test = [] {
            const auto orig = uint8_vector::from_hex("411134F9ACC87389270CBE27BB0988D6B532CD705C9919719804D3E800FFFFFFFE808080808089CAC3640200");
            const auto exp = uint8_vector::from_hex("411134F9ACC87389270CBE27BB0988D6B532CD705C9919719804D3E80089CAC3640200");
            const address addr { orig };
            expect_equal(addr.bytes(), exp);
        };
        "param_update"_test = [] {
            param_update u1 {};
            u1.rehash();
            param_update u2 {};
            u2.rehash();
            expect(u1.hash == u2.hash);
            expect(u1 == u2);
            u1.decentralization.emplace(2, 10);
            u1.rehash();
            expect(u1.hash != u2.hash);
            expect(u1 != u2);
            u2.decentralization.emplace(2, 10);
            u2.rehash();
            expect(u1.hash == u2.hash);
            expect(u1 == u2);
        };
        "slot"_test = [] {
            const configs_dir cfgs { "etc/sanchonet" };
            const cardano::config c_cfg { cfgs };
            expect(c_cfg.conway_protocol_params.plutus_cost_models.at(2).size() == 233);
            expect(!c_cfg.conway_constitution.policy_id.has_value());
            const slot s1 { 86300, c_cfg };
            const slot s2 { 86400, c_cfg };
            expect(s1.epoch() != s2.epoch());
            expect(s1.chunk_id() != s2.chunk_id());
        };
        "byron redeem address #1"_test = [] {
            const std::string_view redeem_vk { "AAG3vJwTzCcL0zp2-1yfI-mn_7haYvSYJln2xR_aBS8=" };
            const auto my_addr_protected = byron_avvm_addr(redeem_vk);
            const auto addr_protected = from_haskell("\\130\\216\\CANX!\\131X\\FS\\131\\132\\243\\236FaTKh\\199w\\132\\246\\148\\206*\\212\\226\\tpW\\184\\162\\224\\186;6\\185\\160\\STX\\SUBp\\f7m");
            expect_equal(addr_protected, my_addr_protected);
        };
        "byron redeem address"_test = [] {
            struct test_vec {
                std::string vk {};
                std::string tx_id {};
            };
            std::vector<test_vec> test_vectors {
                { "-0BJDi-gauylk4LptQTgjMeo7kY9lTCbZv12vwOSTZk=", "8EE33C9906974706223D7D500D63BBEE2369D7150F972757A9FDDED2F706B938" },
                { "DKsNZvWNRoVbR4wIVAM2XE3IQ8vyPeE1Q8oRtraauiM=", "633BEEF5CE862F414E61C4963F57B266B7A5C46F91274141DBA6CC1063C74204" }
            };
            for (const auto &[vk, tx_id]: test_vectors) {
                expect_equal(tx_hash::from_hex(tx_id), byron_avvm_tx_hash(vk));
            }
        };
    };
};
