/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <atomic>
#include <turbo/common/test.hpp>
#include <turbo/cardano/ledger/state.hpp>
#include <turbo/cardano/common/block-producer.hpp>
#include <turbo/index/utxo.hpp>
#include <turbo/storage/test.hpp>
#include <turbo/sync/mocks.hpp>
#include "validator.hpp"
#include "turbo/chunk-registry.hpp"

namespace {
    using namespace turbo;
}

suite txwit_validator_suite = [] {
    "txwit::validator"_test = [] {
        static const std::string src_dir { turbo::storage::sample_registry_path() };
        const chunk_registry cr { src_dir, chunk_registry_settings_t { .mode=chunk_registry::mode::store } };
        const cardano::optional_point target { cr.find_block_by_slot(7167).point() };
        expect_equal(txwit::validate(cr, {}, target), target);

        "body size limits exclude the header and require its declared size"_test = [] {
            for (const auto [limit, declared]: { std::pair{4U, 4U}, {3U, 4U}, {4U, 5U} }) {
                auto genesis = json::load(install_path("etc/mainnet/shelley-genesis.json")).as_object();
                genesis.at("protocolParams").as_object().at("maxBlockBodySize") = limit;
                auto chain = sync::gen_chain({ .height=1,
                    .cfg={{"shelley-genesis", config_json{std::move(genesis)}}} });
                chain.cardano_cfg.set_shelley_start_epoch(0);
                if (declared != 4) {
                    auto parsed = cbor::zero2::parse(chain.data);
                    auto &envelope = parsed.get().array();
                    envelope.read(); // era
                    auto &fields = envelope.read().array().read().array().read().array();
                    for (size_t i = 0; i < 6; ++i)
                        fields.read();
                    const auto encoded = fields.read().data_raw();
                    expect(fatal(encoded.size() == 1 && encoded[0] == 4));
                    chain.data[encoded.data() - chain.data.data()] = static_cast<uint8_t>(declared);
                }
                const file::tmp_directory dir{"txwit-body-size"};
                storage::chunk_fixture_t fixture{dir.path(), chain.cardano_cfg};
                fixture.add(0, chain.data);
                fixture.save();
                scheduler sched{4};
                const chunk_registry source{dir.path(), chunk_registry_settings_t{
                    .mode=chunk_registry::mode::store, .ccfg=chain.cardano_cfg, .sched=sched
                }};
                cardano::ledger::state st{source.config(), sched};
                for (const auto &[key, delegate]: source.config().shelley_delegates)
                    st.propose_update(1, { .key_id=key,
                        .update={ .protocol_ver=cardano::protocol_version{8, 0} }, .epoch=0 });
                st.start_epoch(0);
                expect(fatal(st.params().max_block_body_size == limit));
                txwit::processor proc{source, st};
                storage::chunk_cptr_list chunks{&source.chunks().begin()->second};
                if (limit == 4 && declared == 4) {
                    expect(nothrow([&] { proc.apply(chunks); }));
                    expect_equal(st.end_offset(), chain.data.size());
                } else {
                    std::string failure;
                    try {
                        proc.apply(chunks);
                    } catch (const std::exception &ex) {
                        failure = ex.what();
                    }
                    expect(failure.find(limit == 3 ? "block body size of 4 exceeded the limit of 3"
                        : "has body size 4 but its header declares 5") != std::string::npos) << failure;
                }
            }
        };

        "Babbage collateral returns can be spent later in the same block"_test = [] {
            cardano::config cfg { cardano::config::get() };
            cfg.set_shelley_start_epoch(0);
            const auto seed = crypto::blake2b::digest<crypto::ed25519::seed>(std::string_view { "collateral-return" });
            const auto sk = crypto::ed25519::create_sk_from_seed(seed);
            const auto vk = crypto::ed25519::extract_vk(sk);
            uint8_vector address { 0x61 };
            address << crypto::blake2b::digest<cardano::key_hash>(vk);
            cardano::block_producer block { sk, seed, cardano::vrf03_create_sk_from_seed(seed) };
            block.prev_hash = cfg.byron_genesis_hash;
            block.txs.push_back({ .outputs={{address, 1}}, .collateral={{{}, 0, sk, vk}},
                .collateral_return=cardano::block_producer::tx_output { address, 100 }, .invalid=true });
            const cardano::parsed_block first { block.cbor(), cfg };
            const cardano::tx_out_ref returned { first.blk->txs().front()->hash(), 1 };
            block.txs.push_back({ {{returned.hash, returned.idx, sk, vk}}, {{address, 100}} });
            const auto bytes = block.cbor();
            const cardano::parsed_block parsed { bytes, cfg };

            const file::tmp_directory dir { "same-block-collateral-return" };
            scheduler sched { 4 };
            index::utxo::indexer index { dir.path(), "utxo", sched };
            {
                indexer::chunk_indexer_list_t indices;
                indices.emplace_back(index.make_chunk_indexer("update", 0));
                indices.index_block(parsed.blk);
            }
            cardano::txo_map indexed;
            turbo::zpp::load_zstd(indexed, index.chunk_path("update", 0) + ".bin");
            expect_equal(indexed.size(), 1);
            expect(!indexed.contains(returned));

            storage::chunk_fixture_t fixture { dir.path(), cfg };
            fixture.add(0, bytes);
            fixture.save();
            const chunk_registry source { dir.path(), chunk_registry_settings_t {
                .mode=chunk_registry::mode::store, .ccfg=cfg, .sched=sched
            } };
            cardano::ledger::state st { source.config(), sched, cardano::ledger::state::init_mode::empty };
            for (const auto &[key, delegate]: cfg.shelley_delegates)
                st.propose_update(1, { .key_id=key,
                    .update={ .min_fee_a=0, .min_fee_b=0, .protocol_ver=cardano::protocol_version { 8, 0 } }, .epoch=0 });
            st.start_epoch(0);
            cardano::ledger::utxo_update_list initial(1);
            initial[0].try_emplace(cardano::tx_out_ref { {}, 0 }, cardano::tx_out_data { .address_raw=address, .coin=150 });
            st.process_utxo_updates(std::move(initial));
            txwit::processor proc { source, st };
            proc.apply({ &source.chunks().begin()->second }, {}, {}, txwit::witness_type::none);
            expect_equal(st.end_offset(), bytes.size());
            expect(st.utxos() == indexed);
        };

        "direct and queued batches reject invalid signatures before ledger application"_test = [] {
            const auto seed = crypto::blake2b::digest<crypto::ed25519::seed>(std::string_view { "signature-worker" });
            const auto sk = crypto::ed25519::create_sk_from_seed(seed);
            const auto other_seed = crypto::blake2b::digest<crypto::ed25519::seed>(std::string_view { "wrong-key" });
            const auto wrong_vk = crypto::ed25519::extract_vk(crypto::ed25519::create_sk_from_seed(other_seed));
            for (const size_t num_chunks: { 1, 2 }) {
                const file::tmp_directory dir { "txwit-parallel-signatures" };
                storage::chunk_fixture_t fixture { dir.path() };
                cardano::block_producer block { sk, seed, cardano::vrf03_create_sk_from_seed(seed) };
                block.prev_hash = cardano::config::get().byron_genesis_hash;
                // The signature is made with sk but advertises another key.
                block.txs.push_back({ { { {}, 0, sk, wrong_vk } }, {} });
                size_t offset = 0;
                for (size_t i = 0; i < num_chunks; ++i) {
                    uint8_vector chunk_bytes;
                    for (size_t j = 0; j < 8; ++j) {
                        block.height = i * 8 + j;
                        block.slot = block.height * 2;
                        block.txs.front().inputs.front().txo_idx = cardano::tx_out_idx { block.height };
                        const auto bytes = block.cbor();
                        cbor::zero2::decoder dec { bytes };
                        const cardano::block_container parsed { offset + chunk_bytes.size(), dec.read() };
                        block.prev_hash = parsed->hash();
                        chunk_bytes << bytes;
                    }
                    expect(fatal(chunk_bytes.size() < (1U << 20)));
                    fixture.add(offset, chunk_bytes);
                    offset += chunk_bytes.size();
                }
                fixture.save();
                scheduler sched { 4 };
                const chunk_registry source { dir.path(), chunk_registry_settings_t {
                    .mode=chunk_registry::mode::store, .sched=sched
                } };
                storage::chunk_cptr_list chunks;
                for (const auto &[id, chunk]: source.chunks())
                    chunks.push_back(&chunk);
                cardano::ledger::state st { source.config(), sched };
                st.start_epoch(0);
                std::atomic_size_t signature_errors { 0 };
                txwit::processor proc { source, st, [&](const auto &message) {
                    if (message.find("shelley vkey witness failed") != std::string::npos)
                        ++signature_errors;
                    throw error(message);
                } };
                // One small chunk uses the direct path; two use compressed,
                // queued batches whose raw block views outlive preparation.
                expect(throws([&] { proc.apply(chunks); }));
                expect(signature_errors.load() > 0);
                expect_equal(st.end_offset(), 0);
                expect_equal(proc.end_offset(), 0);
                expect(!sched.process_ok(false));
            }
        };

        "epoch setup gates ledger application"_test = [&] {
            cardano::ledger::state st { cr.config(), cr.sched() };
            txwit::processor proc { cr, st };
            storage::chunk_cptr_list chunks;
            for (const auto &[id, chunk]: cr.chunks()) {
                if (chunk.offset >= target->end_offset)
                    break;
                chunks.push_back(&chunk);
            }
            size_t setup_calls = 0;
            proc.apply({}, {}, target, txwit::witness_type::all, [&] {
                ++setup_calls;
                return false;
            });
            expect_equal(setup_calls, 1);
            proc.apply(chunks, {}, target, txwit::witness_type::all, [&] {
                ++setup_calls;
                return false;
            });
            expect_equal(setup_calls, 2);
            expect_equal(st.end_offset(), 0);
            expect(throws([&] {
                proc.apply(chunks, {}, target, txwit::witness_type::all, [&]() -> bool {
                    throw error("epoch setup failed");
                });
            }));
            expect_equal(st.end_offset(), 0);
            proc.apply(chunks, {}, target, txwit::witness_type::all, [&] {
                ++setup_calls;
                expect_equal(st.end_offset(), 0);
                st.start_epoch(0);
                return true;
            });
            expect_equal(setup_calls, 3);
            expect_equal(proc.end_offset(), target->end_offset);
            expect_equal(st.end_offset(), target->end_offset);
        };
    };
};
