/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/block.hpp>
#include <turbo/common/test.hpp>
#include <turbo/zpp.hpp>

using namespace turbo;
using namespace turbo::cardano;

namespace {
    template<typename T>
    void expect_decodes(const std::string_view hex)
    {
        const auto raw = uint8_vector::from_hex(hex);
        cbor::zero2::decoder dec { raw };
        static_cast<void>(T::from_cbor(dec.read()));
        expect(dec.done());
    }

    template<typename DIJKSTRA, typename PREVIOUS>
    void check_indefinite(const std::string_view hex)
    {
        expect_decodes<DIJKSTRA>(hex);
        auto raw = uint8_vector::from_hex(hex);
        expect(throws([&] { PREVIOUS::from_cbor(cbor::zero2::parse(raw).get()); }));
        raw.back() = 0;
        raw << 0xFF;
        expect(throws([&] { DIJKSTRA::from_cbor(cbor::zero2::parse(raw).get()); }));
    }

    template<typename T>
    void expect_encoding(const std::string_view input, const std::string_view expected,
        const era_t era = era_t::dijkstra)
    {
        const auto raw = uint8_vector::from_hex(input);
        cbor::zero2::decoder dec { raw,
            era == era_t::dijkstra ? cbor::zero2::decoder::create_chunk_storage() : nullptr };
        const auto value = T::from_cbor(dec.read());
        expect(dec.done());
        era_encoder enc { era };
        value.to_cbor(enc);
        expect_equal(enc.cbor(), uint8_vector::from_hex(expected));
    }
}

suite cardano_dijkstra_decoding_suite = [] {
    "cardano::dijkstra::decoding"_test = [] {
        const std::string hash32(64, '0');
        const std::string hash28(56, '0');
        const auto anchor = "9F605820" + hash32 + "FF";
        const auto action_id = "9F5820" + hash32 + "00FF";
        const auto constitution = "9F" + anchor + "F6FF";
        const auto vote = "9F01" + anchor + "FF";
        const std::string datum = "9F01D8185F4100FFFF";

        "era-specific encodings"_test = [&] {
            check_indefinite<dijkstra::ex_units, ex_units>("9F0102FF");
            check_indefinite<dijkstra::ex_unit_prices, ex_unit_prices>("9FD81E820001D81E820102FF");
            check_indefinite<dijkstra::anchor_t, anchor_t>(anchor);
            check_indefinite<dijkstra::gov_action_id_t, gov_action_id_t>(action_id);
            check_indefinite<dijkstra::constitution_t, constitution_t>(constitution);
            check_indefinite<dijkstra::voting_procedure_t, voting_procedure_t>(vote);
            check_indefinite<dijkstra::datum_option_t, datum_option_t>(datum);
        };
        "nested era-specific encodings"_test = [&] {
            const auto credential = "8200581C" + hash28;
            expect_decodes<dijkstra::governance_action_t>("8305" + action_id + constitution);
            expect_decodes<dijkstra::governance_action_t>("8203F6");
            expect_decodes<dijkstra::certificate_t>("830F" + credential + anchor);
            expect_decodes<dijkstra::certificate_t>("8410" + credential + "00" + anchor);
            expect_decodes<dijkstra::certificate_t>("8312" + credential + anchor);
            expect_decodes<dijkstra::proposal_procedures_t>("818400581DE0" + hash28 + "8106" + anchor);
            expect_decodes<dijkstra::voting_procedures_t>("A1" + credential + "A1" + action_id + vote);
            expect_decodes<dijkstra::voting_procedure_t>("8201F6");
            expect_decodes<dijkstra::transaction_output_t>("A300581D60" + hash28 + "010002" + datum);
            expect_decodes<dijkstra::protocol_param_update_t>("A1149F0102FF");
            expect_decodes<dijkstra::redeemers_t>("A182000082009F0102FF");
        };
        "inline datum bytes and output forms"_test = [&] {
            const auto addr = "581D60" + hash28;
            const std::string inline_datum = "8201D818439F00FF";
            expect_encoding<dijkstra::datum_option_t>("9F01D8185F419F4200FFFFFF", inline_datum);
            expect_encoding<datum_option_t>(inline_datum, inline_datum, era_t::conway);
            for (const auto era: { era_t::conway, era_t::dijkstra }) {
                const auto check = [&](const std::string &input, const std::string &expected) {
                    if (era == era_t::dijkstra)
                        expect_encoding<dijkstra::transaction_output_t>(input, expected, era);
                    else
                        expect_encoding<conway::transaction_output_t>(input, expected, era);
                };
                check("A200" + addr + "0100", "82" + addr + "00");
                check("9F" + addr + "005820" + hash32 + "FF", "83" + addr + "005820" + hash32);
                const auto output = "A300" + addr + "010002" + inline_datum;
                check(output, output);
                expect_encoding<conway::cost_models_t>("A0", "A0", era);
            }
        };
        "ledger map ordering"_test = [&] {
            const auto key = "8200581C" + hash28;
            const auto script = "8201581C" + std::string(56, '1');
            const auto script_low = "8201581C" + hash28;
            const auto intervals = "A3" + key + "00" + script + "01" + script_low + "02";
            const auto ordered_intervals = "A3" + script_low + "02" + script + "01" + key + "00";
            // Guards exercise both absent and present optional Plutus data.
            const auto guards = "A3" + key + "F6" + script + "01" + script_low + "02";
            const auto ordered_guards = "A3" + script_low + "02" + script + "01" + key + "F6";
            const std::string body = "A600D901028001800200";
            expect_encoding<dijkstra::transaction_body_t>(
                body + "1818" + guards + "181A" + intervals + "181B" + intervals,
                body + "1818" + ordered_guards + "181A" + ordered_intervals + "181B" + ordered_intervals);
            const auto key0 = "581DE0" + hash28;
            const auto script0 = "581DF0" + std::string(56, '1');
            const auto key1 = "581DE1" + hash28;
            const auto script1 = "581DF1" + hash28;
            const auto accounts = "A4" + key0 + "01" + key1 + "02" + script0 + "03" + script1 + "04";
            const auto ordered_accounts = "A4" + script0 + "03" + key0 + "01" + script1 + "04" + key1 + "02";
            expect_encoding<dijkstra::direct_deposits_t>(accounts, ordered_accounts);
            const std::string withdrawals = "A400D90102800180020005";
            expect_encoding<conway::transaction_body_t>(
                withdrawals + accounts, withdrawals + ordered_accounts, era_t::conway);
            const auto fields = "05" + accounts + "1819" + accounts;
            const auto ordered_fields = "05" + ordered_accounts + "1819" + ordered_accounts;
            const std::string tx_body = "A500D901028001800200";
            expect_encoding<dijkstra::transaction_body_t>(tx_body + fields, tx_body + ordered_fields);
            const std::string sub_body = "A400D90102800180";
            expect_encoding<dijkstra::sub_transaction_body_t>(sub_body + fields, sub_body + ordered_fields);
            const auto treasury = "8302" + accounts + "F6";
            const auto ordered_treasury = "8302" + ordered_accounts + "F6";
            expect_encoding<gov_action_t>(treasury, ordered_treasury, era_t::conway);
            expect_encoding<dijkstra::governance_action_t>(treasury, ordered_treasury);
        };
        "committee credentials use natural ledger order in both eras"_test = [&] {
            const auto key = "8200581C" + hash28;
            const auto script = "8201581C" + std::string(56, 'F');
            const auto script_low = "8201581C" + hash28;
            const auto input = "8504F6D9010283" + key + script + script_low
                + "A3" + key + "00" + script + "01" + script_low + "02D81E820102";
            const auto expected = "8504F6D9010283" + script_low + script + key
                + "A3" + script_low + "02" + script + "01" + key + "00D81E820102";
            expect_encoding<gov_action_t>(input, expected, era_t::conway);
            expect_encoding<dijkstra::governance_action_t>(input, expected);
        };
        "voter ordering and grouping"_test = [&] {
            const auto action0 = "825820" + hash32 + "00";
            const auto action1 = "825820" + hash32 + "01";
            const auto votes = "A2" + action1 + "8201F6" + action0 + "8200F6";
            const auto ordered_votes = "A2" + action0 + "8200F6" + action1 + "8201F6";
            const auto voter = [&](const unsigned type) {
                return "820" + std::to_string(type) + "581C" + hash28;
            };
            std::string input = "A5";
            std::string expected = "A5";
            for (const auto type: { 0, 1, 2, 3, 4 })
                input += voter(type) + votes;
            for (const auto type: { 1, 0, 3, 2, 4 })
                expected += voter(type) + ordered_votes;
            expect_encoding<dijkstra::voting_procedures_t>(input, expected);
            expect_encoding<conway::voting_procedures_t>(input, expected, era_t::conway);
        };
        "header version information uses two unsigned 32-bit fields"_test = [] {
            const auto raw = uint8_vector::from_hex("9F1AFFFFFFFF1AFFFFFFFFFF");
            const auto info = dijkstra::header_version_info_t::from_cbor(cbor::zero2::parse(raw).get());
            expect_equal(info.highest_supported_major_version, 0xFFFFFFFFULL);
            expect_equal(info.self_reported_software_tag, 0xFFFFFFFFULL);
            expect_encoding<dijkstra::header_version_info_t>("9F0E1904D2FF", "820E1904D2");
            for (const auto malformed: { "821B000000010000000000", "82001B0000000100000000", "810C", "830C0000" })
                expect(throws([&] { expect_decodes<dijkstra::header_version_info_t>(malformed); }));
        };
        "Leios and dormant parameters retain their typed values through encoding and snapshots"_test = [] {
            const std::string parameters = "B1"
                "182801182902182A03182B04182CD81E820102182D06182E07182F820809"
                "18300A18310B1832D81E820C0118330D18340E18350F1836D81E820104183711183812";
            const auto raw = uint8_vector::from_hex(parameters);
            const auto decoded = dijkstra::protocol_param_update_t::from_cbor(cbor::zero2::parse(raw).get());
            param_update_t expected {};
            expected.leios_announcement_period_length = 1;
            expected.leios_vote_period_length = 2;
            expected.leios_diffusion_period_length = 3;
            expected.leios_committee_size = 4;
            expected.leios_quorum_stake_threshold = rational_u64 { 1, 2 };
            expected.max_endorser_block_references_size = 6;
            expected.max_endorser_block_txs_size = 7;
            expected.max_endorser_block_ex_units = ex_units { 8, 9 };
            expected.max_ref_script_size_per_endorser_block = 10;
            expected.peras_min_candidate_block_age = 11;
            expected.peras_healing_factor = rational_u64 { 12, 1 };
            expected.peras_cert_boost = 13;
            expected.peras_target_committee_size = 14;
            expected.peras_bootstrap_round.emplace(nil_optional_t<uint32_t> { 15 });
            expected.peras_quorum_threshold_safety_margin = rational_u64 { 1, 4 };
            expected.ref_inputs_cost_per_multi_asset_policy = 17;
            expected.ref_inputs_cost_per_datum_byte = 18;
            expect(decoded.value == expected);
            expect_encoding<dijkstra::protocol_param_update_t>(parameters, parameters);
            expect_encoding<dijkstra::governance_action_t>("8400F6" + parameters + "F6", "8400F6" + parameters + "F6");
            expect(turbo::zpp::deserialize<param_update_t>(turbo::zpp::serialize(decoded.value)) == expected);
            const auto null_raw = uint8_vector::from_hex("A11835F6");
            const auto null_update = dijkstra::protocol_param_update_t::from_cbor(cbor::zero2::parse(null_raw).get());
            expect(null_update.value.peras_bootstrap_round.has_value());
            expect(!null_update.value.peras_bootstrap_round->has_value());
            expect_encoding<dijkstra::protocol_param_update_t>("A11835F6", "A11835F6");
            expect_encoding<dijkstra::protocol_param_update_t>("A1183500", "A1183500");
            for (const auto malformed: {
                "A118281B0000000100000000", // 32-bit overflow
                "A1182B1A00010000", // 16-bit overflow
                "A1182CD81E820201", // quorum above one
                "A1182CD81E820100", // zero denominator
                "A11832D81E820001", // non-positive healing factor
                "A118351B0000000100000000", // nullable round overflow
                "A2182800182801", // duplicate key
                "A1183900" // unknown key
            })
                expect(throws([&] { expect_decodes<dijkstra::protocol_param_update_t>(malformed); }));
            expect(throws([&] { expect_decodes<param_update_t>(parameters); }));
        };
        "Dijkstra VRF output length"_test = [] {
            const auto decode = [](const size_t size) {
                cbor::encoder enc {};
                enc.array(2).array(12).uint(0).uint(0).s_null().bytes(vkey {}).bytes(vrf_vkey {});
                enc.array(2).bytes().bytes(uint8_vector(size)).s_break().bytes(vrf_proof {});
                enc.uint(0).bytes(block_hash {}).array(4).bytes(kes_vkey {}).uint(0).uint(0);
                enc.bytes(crypto::ed25519::signature {}).array(2).uint(12).uint(0).s_false().s_null();
                enc.bytes(cardano_kes_signature_data {});
                cbor::zero2::decoder dec { enc.cbor(), cbor::zero2::decoder::create_chunk_storage() };
                const dijkstra::block_header header { 8, dec.read(), cardano::config::get() };
                expect(dec.done());
            };
            decode(64);
            expect(throws([&] { decode(63); }));
            expect(throws([&] { decode(65); }));
        };
        "Leios signer bitfield length"_test = [] {
            for (const size_t size: { 0, 8192, 8193 }) {
                cbor::encoder enc {};
                enc.array(2).bytes().bytes(uint8_vector(size)).s_break().bytes(byte_array<48> {});
                cbor::zero2::decoder dec { enc.cbor(), cbor::zero2::decoder::create_chunk_storage() };
                const auto decode = [&] {
                    const auto cert = dijkstra::leios_certificate_t::from_cbor(dec.read());
                    expect_equal(cert.signers.size(), size);
                    expect(dec.done());
                };
                if (size <= 8192)
                    decode();
                else
                    expect(throws(decode));
            }
        };
    };
};
