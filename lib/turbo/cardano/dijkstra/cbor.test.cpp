/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/block.hpp>
#include <turbo/cardano.hpp>
#include <turbo/cardano/common/block-producer.hpp>
#include <turbo/cbor/normalize.hpp>
#include <turbo/common/test.hpp>

namespace {
    using namespace turbo;
    using namespace cardano;

    template<typename T>
    T decode(const buffer bytes) {
        cbor::zero2::decoder dec { bytes, cbor::zero2::decoder::create_chunk_storage() };
        auto value = T::from_cbor(dec.read());
        expect(dec.done());
        return value;
    }

    template<typename T>
    void check_preserved(const std::string_view hex) {
        const auto bytes = uint8_vector::from_hex(hex);
        const auto value = decode<T>(bytes);
        // Both the decoder and its concatenated chunk storage have been destroyed.
        expect(value.original_cbor().data() == bytes.data());
        era_encoder enc { era_t::dijkstra };
        value.to_cbor(enc);
        expect_equal(enc.cbor(), bytes);
    }

    uint8_vector make_header(const buffer body, const bool leios = false,
        const std::optional<dijkstra::eb_announcement_t> &announcement = {})
    {
        cbor::encoder enc {};
        enc.array().array(12).uint(0).uint(0).s_null().bytes(vkey {}).bytes(vrf_vkey {})
            .array(2).bytes(vrf_result {}).bytes(vrf_proof {}).uint(body.size())
            .bytes(crypto::blake2b::digest<block_hash>(body))
            .array(4).bytes(kes_vkey {}).uint(0).uint(0).bytes(signature {})
            .array(2).uint(14).uint(1234).boolean(leios);
        if (announcement)
            enc.array(2).bytes(announcement->hash).uint(announcement->size);
        else
            enc.s_null();
        enc.bytes(cardano_kes_signature_data {}).s_break();
        return enc.cbor();
    }
}

suite cardano_dijkstra_cbor_suite = [] {
    "cardano::dijkstra::cbor"_test = [] {
        const auto native = "9F005F581C" + std::string(56, '0') + "FFFF";
        const std::string data = "5F41004101FF";
        const std::string redeemers = "BF820000829F00FF820102FF";
        const auto witnesses = "BF0181" + native + "0481" + data + "05" + redeemers + "FF";
        const std::string auxiliary = "BF0000FF";
        const std::string sub_body = "BF00800180FF";
        const auto sub_tx = "83" + sub_body + witnesses + auxiliary;
        const auto body = "BF008001800218001781" + sub_tx + "FF";
        const auto transaction = "83" + body + witnesses + auxiliary;

        "DijkstraNet Praos headers preserve their signing bytes"_test = [] {
            const auto seed = crypto::blake2b::digest<crypto::ed25519::seed>(std::string_view { "dijkstra-header" });
            block_producer producer { crypto::ed25519::create_sk_from_seed(seed), seed, vrf03_create_sk_from_seed(seed) };
            const auto bytes = producer.empty_dijkstra_cbor();
            const parsed_block parsed { bytes };
            const auto &header = dynamic_cast<const dijkstra::block_header &>(parsed.blk->header());
            expect(!header.leios());
            expect(header.protocol_ver() == protocol_version { 12, 0 });
            expect(parsed.blk->signature_ok());
            expect(parsed.blk->body_hash_ok());
            era_encoder enc { era_t::dijkstra };
            header.to_cbor(enc);
            expect_equal(buffer { enc.cbor() }, header.raw());
        };

        "validated wire values borrow their original bytes"_test = [&] {
            check_preserved<decoded_cbor_t<dijkstra::native_script_t>>(native);
            check_preserved<decoded_cbor_t<dijkstra::native_script_t>>(
                "9F068201581C" + std::string(56, '0') + "FF");
            check_preserved<dijkstra::plutus_data_t>(data);
            check_preserved<decoded_cbor_t<dijkstra::auxiliary_data_t>>(auxiliary);
            check_preserved<decoded_cbor_t<dijkstra::redeemers_t>>(redeemers);
            check_preserved<decoded_cbor_t<dijkstra::transaction_witness_set_t>>(witnesses);
            check_preserved<decoded_cbor_t<dijkstra::transaction_body_t>>(body);
            check_preserved<decoded_cbor_t<dijkstra::sub_transaction_body_t>>(sub_body);
            expect(throws([] {
                const auto invalid = uint8_vector::from_hex("A0");
                decode<decoded_cbor_t<dijkstra::transaction_body_t>>(invalid);
            }));
        };

        "transactions preserve nested components while rebuilding their envelope"_test = [&] {
            const auto bytes = uint8_vector::from_hex("84" + body + witnesses + "F5" + auxiliary);
            const auto tx = decode<dijkstra::transaction_t>(bytes);
            era_encoder enc { era_t::dijkstra };
            tx.to_cbor(enc);
            expect_equal(enc.cbor(), uint8_vector::from_hex(transaction));
            expect(tx.body.original_cbor().data() == bytes.data() + 1);
            expect_equal(tx.body.value().sub_transactions.size(), 1);

            // Editing a semantic copy still preserves its decoded sub-transactions.
            auto edited_body = tx.body.value();
            edited_body.fee = 1;
            era_encoder edited { era_t::dijkstra };
            edited_body.to_cbor(edited);
            const auto result = decode<dijkstra::transaction_body_t>(edited.cbor());
            expect_equal(result.fee, 1);
            era_encoder nested { era_t::dijkstra };
            result.sub_transactions.at(0).to_cbor(nested);
            expect_equal(nested.cbor(), uint8_vector::from_hex(sub_tx));

            const auto sub_bytes = uint8_vector::from_hex(sub_body);
            const auto decoded_sub = decode<decoded_cbor_t<dijkstra::sub_transaction_body_t>>(sub_bytes);
            era_encoder sub_enc { era_t::dijkstra };
            decoded_sub.to_cbor(sub_enc);
            // The corpus compares this rule after normalization, unlike transaction_body.
            expect_equal(cbor::normalize(sub_enc.cbor()), uint8_vector::from_hex("A200800180"));
            expect(throws([&] {
                const auto invalid = uint8_vector::from_hex("84" + body + witnesses + "F4" + auxiliary);
                decode<dijkstra::transaction_t>(invalid);
            }));
        };

        "semantic encoders retain validated payloads without reparsing"_test = [&] {
            const auto script = [&] {
                const auto bytes = uint8_vector::from_hex("9F00" + native + "FF");
                return decode<dijkstra::script_t>(bytes);
            }();
            era_encoder script_enc { era_t::dijkstra };
            script.to_cbor(script_enc);
            expect_equal(script_enc.cbor(), uint8_vector::from_hex("8200" + native));

            const auto wits = [&] {
                const auto bytes = uint8_vector::from_hex(witnesses);
                return decode<dijkstra::transaction_witness_set_t>(bytes);
            }();
            era_encoder wits_enc { era_t::dijkstra };
            wits.to_cbor(wits_enc);
            expect_equal(wits_enc.cbor(), uint8_vector::from_hex(
                "A301D9010281" + native + "04D9010281" + data + "05A1820000829F00FF820102"));

            const auto aux_bytes = uint8_vector::from_hex("D90103A10181" + native);
            const auto aux = decode<dijkstra::auxiliary_data_t>(aux_bytes);
            era_encoder aux_enc { era_t::dijkstra };
            aux.to_cbor(aux_enc);
            expect_equal(aux_enc.cbor(), aux_bytes);

            // The embedded decoder must support chunked key bytes. The output owns
            // its script after both the source and the decoder's storage are gone.
            const auto reference = uint8_vector::from_hex("8200" + native);
            era_encoder output_enc { era_t::dijkstra };
            output_enc.map(3).uint(0).bytes(uint8_vector::from_hex("60" + std::string(56, '0')))
                .uint(1).uint(0).uint(3).tag(24).bytes(reference);
            const auto output = [&] {
                const auto bytes = output_enc.cbor();
                return decode<dijkstra::transaction_output_t>(bytes);
            }();
            era_encoder encoded { era_t::dijkstra };
            output.to_cbor(encoded);
            expect_equal(encoded.cbor(), output_enc.cbor());

            const auto guarded_bytes = uint8_vector::from_hex(
                "A400D9010280018002001818A18200581C" + std::string(56, '0') + data);
            const auto guarded = decode<dijkstra::transaction_body_t>(guarded_bytes);
            era_encoder guarded_enc { era_t::dijkstra };
            guarded.to_cbor(guarded_enc);
            expect_equal(guarded_enc.cbor(), guarded_bytes);
        };

        "ledger size excludes validity and normalizes only the outer envelope"_test = [&] {
            for (const bool definite: { false, true }) {
                for (const bool valid: { false, true }) {
                    for (const auto &aux: { std::string { "F6" }, auxiliary }) {
                        const auto tx_bytes = uint8_vector::from_hex(
                            (definite ? "84" : "9F") + body + witnesses + aux
                            + (valid ? "F5" : "F4") + (definite ? "" : "FF"));
                        cbor::encoder body_enc {};
                        body_enc.array(3).array(1).raw_cbor(tx_bytes).s_null().s_null();
                        const auto header = make_header(body_enc.cbor());
                        cbor::encoder encoded {};
                        encoded.array(2).raw_cbor(header).raw_cbor(body_enc.cbor());
                        cbor::zero2::decoder dec { encoded.cbor(), cbor::zero2::decoder::create_chunk_storage() };
                        const dijkstra::block block { 8, 0, 0, dec.read(), cardano::config::get() };
                        expect(dec.done());
                        const auto &tx = dynamic_cast<const dijkstra::tx &>(*block.txs().at(0));
                        const auto expected = uint8_vector::from_hex("83" + body + witnesses + aux);
                        expect_equal(tx.ledger_size(), expected.size());
                        expect_equal(tx.size(), tx_bytes.size());
                        expect_equal(tx.size() - tx.ledger_size(), definite ? 1U : 2U);
                        era_encoder mempool { era_t::dijkstra };
                        tx.to_cbor(mempool);
                        expect_equal(mempool.cbor(), expected);
                    }
                }
            }
        };

        "block bytes, validity and Leios metadata survive decoding and storage lookup"_test = [&] {
            const auto valid_tx = uint8_vector::from_hex("84" + body + witnesses + auxiliary + "F5");
            const auto invalid_tx = uint8_vector::from_hex("84" + body + witnesses + auxiliary + "F4");
            const auto signers = uint8_vector::from_hex("0102");
            const auto peras = uint8_vector::from_hex("0304");
            const dijkstra::eb_announcement_t announcement { block_hash::from_hex(std::string(64, '1')), 123 };
            cbor::encoder body_enc {};
            body_enc.array().array(2).raw_cbor(valid_tx).raw_cbor(invalid_tx)
                .array(2).bytes(signers).bytes(byte_array<48> {}).bytes(peras).s_break();
            const auto header_bytes = make_header(body_enc.cbor(), true, announcement);
            era_encoder input { era_t::dijkstra };
            input.array().raw_cbor(header_bytes).raw_cbor(body_enc.cbor()).s_break();
            cbor::zero2::decoder dec { input.cbor(), cbor::zero2::decoder::create_chunk_storage() };
            const dijkstra::block block { 8, 0, 0, dec.read(), cardano::config::get() };
            expect(dec.done());
            expect_equal(block.body_size(), body_enc.cbor().size());
            expect_equal(block.body_hash(), block.header().body_hash());
            expect_equal(block.header().hash(), crypto::blake2b::digest<block_hash>(header_bytes));
            expect_equal(block.header().version_info().highest_supported_major_version, 14);
            expect_equal(block.header().version_info().self_reported_software_tag, 1234);
            expect_equal(block.protocol_ver(), (protocol_version { 14, 0 }));
            expect(block.header().body_contains_leios_certificate());
            expect_equal(block.header().eb_announcement()->hash, announcement.hash);
            expect_equal(block.header().eb_announcement()->size, announcement.size);
            expect_equal(block.leios_certificate()->signers, signers);
            expect_equal(*block.peras_certificate(), peras);
            expect_equal(block.invalid_txs().size(), 1);
            expect(block.invalid_txs().contains(1));
            expect(!block.txs().at(0)->invalid());
            expect(block.txs().at(1)->invalid());
            expect_equal(block.txs().at(0)->hash(), block.txs().at(1)->hash());
            expect_equal(block.txs().at(0)->hash(), crypto::blake2b::digest<tx_hash>(uint8_vector::from_hex(body)));
            size_t valid_count = 0;
            size_t invalid_count = 0;
            block.foreach_tx([&](const auto &) { ++valid_count; });
            block.foreach_invalid_tx([&](const auto &) { ++invalid_count; });
            expect_equal(valid_count, 1);
            expect_equal(invalid_count, 1);
            era_encoder encoded_body { era_t::dijkstra };
            block.body_to_cbor(encoded_body);
            expect_equal(encoded_body.cbor(), body_enc.cbor());
            era_encoder encoded { era_t::dijkstra };
            block.to_cbor(encoded);
            era_encoder expected { era_t::dijkstra };
            expected.array(2).raw_cbor(header_bytes).raw_cbor(body_enc.cbor());
            expect_equal(encoded.cbor(), expected.cbor());

            cbor::encoder wrapped {};
            wrapped.array(2).uint(8).raw_cbor(encoded.cbor());
            cbor::zero2::decoder stored_dec { wrapped.cbor(), cbor::zero2::decoder::create_chunk_storage() };
            const block_container stored { 1000, stored_dec.read() };
            const auto &stored_tx = *stored->txs().at(1);
            cbor::zero2::decoder tx_dec { stored_tx.raw(), cbor::zero2::decoder::create_chunk_storage() };
            const tx_container restored { block_info::from_block(stored), stored_tx.offset(),
                tx_dec.read(), 1, cardano::config::get() };
            expect(restored->invalid());
            expect_equal(restored->hash(), stored_tx.hash());
            expect_equal(restored->offset(), stored_tx.offset());
            const auto &typed_tx = static_cast<const dijkstra::tx &>(*restored);
            era_encoder block_tx_enc { era_t::dijkstra };
            typed_tx.to_cbor_for_block(block_tx_enc);
            expect_equal(block_tx_enc.cbor(), invalid_tx);
            era_encoder mempool_enc { era_t::dijkstra };
            typed_tx.to_cbor(mempool_enc);
            expect_equal(mempool_enc.cbor(), uint8_vector::from_hex(transaction));
        };

        "block and mempool transaction envelopes are distinct"_test = [&] {
            const auto check_block = [](const std::string_view body_hex, const bool contains_leios = false) {
                const auto body_bytes = uint8_vector::from_hex(body_hex);
                cbor::encoder enc {};
                enc.array(2).raw_cbor(make_header(body_bytes, contains_leios)).raw_cbor(body_bytes);
                cbor::zero2::decoder dec { enc.cbor() };
                const dijkstra::block block { 8, 0, 0, dec.read(), cardano::config::get() };
                expect(dec.done());
            };
            check_block("8380F6F6");
            check_block("838184A3008001800200A0F6F5F6F6");
            check_block("9F9F9FA3008001800200A0F6F4FFFFF6F6FF");
            for (const auto malformed: {
                "84F680F6F6", // retired block-level validity set
                "838183A3008001800200A0F6F6F6", // missing trailing validity
                "838184A3008001800200A0F5F6F6F6", // mempool compatibility flag
                "838184A3008001800200A0F600F6F6", // non-boolean validity
                "838185A3008001800200A0F6F500F6F6", // extra transaction element
                "8480F6F6F6", // extra body element
                "8280F6" // missing certificate slot
            })
                expect(throws([&] { check_block(malformed); }));
            expect(throws([&] { check_block("8380F6F6", true); }));
            const auto cert = "82405830" + std::string(96, '0');
            expect(throws([&] { check_block("8380" + cert + "F6"); }));
            check_block("8380" + cert + "F6", true);
            expect(throws([&] {
                decode<dijkstra::transaction_t>(uint8_vector::from_hex("84" + body + witnesses + auxiliary + "F5"));
            }));
        };
    };
};
