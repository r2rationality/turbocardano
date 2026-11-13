/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <atomic>
#include <turbo/common/test.hpp>
#include <turbo/cardano/ledger/state.hpp>
#include <turbo/cardano/common/block-producer.hpp>
#include <turbo/index/utxo.hpp>
#include <turbo/index/block-fees.hpp>
#include <turbo/index/timed-update.hpp>
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

        "Dijkstra witness failure cannot be swallowed by the error callback"_test = [] {
            using namespace cardano;
            const configs_dir files { configs_dir::network_path("musashi") };
            const cardano::config cfg { files };
            cbor::encoder body {};
            body.array(3).array(1).array(4)
                .map(3).uint(0).array(1).array(2).bytes(tx_hash {}).uint(0)
                .uint(1).array(0).uint(2).uint(0)
                .map(1).uint(0).array(1).array(2).bytes(vkey {}).bytes(signature {})
                .s_null().boolean(true).s_null().s_null();
            cbor::encoder block {};
            block.array(2).uint(8).array(2).array(2).array(12)
                .uint(0).uint(0).s_null().bytes(vkey {}).bytes(vrf_vkey {})
                .array(2).bytes(vrf_result {}).bytes(vrf_proof {}).uint(body.cbor().size())
                .bytes(crypto::blake2b::digest<block_hash>(body.cbor()))
                .array(4).bytes(kes_vkey {}).uint(0).uint(0).bytes(signature {})
                .array(2).uint(12).uint(0).boolean(false).s_null()
                .bytes(cardano_kes_signature_data {}).raw_cbor(body.cbor());
            const file::tmp_directory dir { "dijkstra-invalid-witness" };
            storage::chunk_fixture_t fixture { dir.path(), cfg };
            fixture.add(0, block.cbor());
            fixture.save();
            scheduler sched { 4 };
            const chunk_registry source { dir.path(), chunk_registry_settings_t {
                .mode=chunk_registry::mode::store, .ccfg=cfg, .sched=sched
            } };
            ledger::state st { cfg, sched };
            bool reported = false;
            txwit::processor proc { source, st, [&](const std::string &message) {
                reported |= message.find("shelley vkey witness failed") != std::string::npos;
            } };
            expect(throws([&] { proc.apply({ &source.chunks().begin()->second }); }));
            expect(reported);
            expect_equal(st.end_offset(), uint64_t { 0 });
        };

        "Dijkstra proposals, votes and donations survive validation and indexing"_test = [] {
            using namespace cardano;
            const configs_dir files { configs_dir::network_path("musashi") };
            const cardano::config cfg { files };
            scheduler sched { 4 };
            ledger::state st { cfg, sched };
            const auto initial_balance = st.utxo_balance();
            const auto seed = crypto::blake2b::digest<crypto::ed25519::seed>(std::string_view { "dijkstra-governance" });
            const auto sk = crypto::ed25519::create_sk_from_seed(seed);
            const auto vk = crypto::ed25519::extract_vk(sk);
            const auto key = crypto::blake2b::digest<key_hash>(vk);
            uint8_vector address { static_cast<uint8_t>(0x60 | cfg.shelley_network_id) };
            address << key;
            uint8_vector reward_address { static_cast<uint8_t>(0xE0 | cfg.shelley_network_id) };
            reward_address << key;
            constexpr uint64_t fee = 1'000'000, donation = 123, output = 5'000'000;
            cbor::encoder tx_body {};
            tx_body.map(6).uint(0).array(1).array(2).bytes(tx_hash {}).uint(0)
                .uint(1).array(1).array(2).bytes(address).uint(output)
                .uint(2).uint(fee)
                .uint(4).array(2).array(3).uint(7).array(2).uint(0).bytes(key).uint(st.params().key_deposit)
                .array(4).uint(16).array(2).uint(0).bytes(key).uint(st.params().drep_deposit).s_null()
                .uint(20).array(1).array(4).uint(st.params().gov_action_deposit).bytes(reward_address)
                .array(1).uint(6).array(2).text("https://example.com/proposal").bytes(datum_hash {})
                .uint(22).uint(donation);
            const auto tx_id = crypto::blake2b::digest<tx_hash>(tx_body.cbor());
            crypto::ed25519::signature sig {};
            crypto::ed25519::sign(sig, tx_id, sk);
            cbor::encoder vote_body {};
            vote_body.map(4).uint(0).array(1).array(2).bytes(tx_id).uint(0)
                .uint(1).array(1).array(2).bytes(address).uint(output - fee)
                .uint(2).uint(fee)
                .uint(19).map(1).array(2).uint(2).bytes(key)
                .map(1).array(2).bytes(tx_id).uint(0).array(2).uint(1).s_null();
            crypto::ed25519::signature vote_sig {};
            crypto::ed25519::sign(vote_sig, crypto::blake2b::digest<tx_hash>(vote_body.cbor()), sk);
            cbor::encoder body {};
            body.array(3).array(2).array(4).raw_cbor(tx_body.cbor())
                .map(1).uint(0).array(1).array(2).bytes(vk).bytes(sig)
                .s_null().boolean(true)
                .array(4).raw_cbor(vote_body.cbor())
                .map(1).uint(0).array(1).array(2).bytes(vk).bytes(vote_sig)
                .s_null().boolean(true).s_null().s_null();
            cbor::encoder block {};
            block.array(2).uint(8).array(2).array(2).array(10)
                .uint(0).uint(0).s_null().bytes(vkey {}).bytes(vrf_vkey {})
                .array(2).bytes(vrf_result {}).bytes(vrf_proof {}).uint(body.cbor().size())
                .bytes(crypto::blake2b::digest<block_hash>(body.cbor()))
                .array(4).bytes(kes_vkey {}).uint(0).uint(0).bytes(signature {})
                .array(2).uint(12).uint(0)
                .bytes(cardano_kes_signature_data {}).raw_cbor(body.cbor());
            const parsed_block parsed { block.cbor(), cfg };
            const file::tmp_directory dir { "dijkstra-governance" };
            index::timed_update::indexer updates { dir.path(), "timed-update", sched };
            index::block_fees::indexer fees { dir.path(), "block-fees", sched };
            {
                indexer::chunk_indexer_list_t indices;
                indices.emplace_back(updates.make_chunk_indexer("update", 0));
                indices.emplace_back(fees.make_chunk_indexer("update", 0));
                indices.index_block(parsed.blk);
            }
            std::vector<index::timed_update::item> indexed_updates;
            turbo::zpp::load_zstd(indexed_updates, updates.chunk_path("update", 0) + ".bin");
            expect_equal(indexed_updates.size(), 6);
            expect(std::ranges::any_of(indexed_updates, [&](const auto &item) {
                const auto *proposal = std::get_if<proposal_t>(&item.update);
                if (!proposal)
                    return false;
                return std::ranges::equal(proposal->id.tx_id, tx_id)
                    && proposal->procedure.deposit == st.params().gov_action_deposit;
            }));
            expect(std::ranges::any_of(indexed_updates, [](const auto &item) {
                const auto *prelude = std::get_if<index::timed_update::conway_tx_prelude>(&item.update);
                return prelude && prelude->has_proposals;
            }));
            expect(std::ranges::any_of(indexed_updates, [&](const auto &item) {
                const auto *vote = std::get_if<conway::vote_info_t>(&item.update);
                if (!vote)
                    return false;
                return vote->action_id == gov_action_id_t { tx_id, 0 } && std::ranges::equal(vote->voter.hash, key);
            }));
            expect(std::ranges::any_of(indexed_updates, [&](const auto &item) {
                const auto *prelude = std::get_if<index::timed_update::conway_tx_prelude>(&item.update);
                return prelude && prelude->voting_dreps.contains(credential_t { key, false });
            }));
            std::vector<index::block_fees::item> indexed_fees;
            turbo::zpp::load_zstd(indexed_fees, fees.chunk_path("update", 0) + ".bin");
            expect(fatal(indexed_fees.size() == 1));
            expect_equal(indexed_fees.front().donations, donation);
            expect_equal(indexed_fees.front().fees, 2 * fee);

            storage::chunk_fixture_t fixture { dir.path(), cfg };
            fixture.add(0, block.cbor());
            fixture.save();
            const chunk_registry source { dir.path(), chunk_registry_settings_t {
                .mode=chunk_registry::mode::store, .ccfg=cfg, .sched=sched
            } };
            ledger::utxo_update_list initial(1);
            initial[0].try_emplace(tx_out_ref { {}, 0 }, tx_out_data { .address_raw=address,
                .coin=output + fee + donation + st.params().key_deposit
                    + st.params().drep_deposit + st.params().gov_action_deposit });
            st.process_utxo_updates(std::move(initial));
            txwit::processor proc { source, st };
            proc.apply({ &source.chunks().begin()->second });
            expect_equal(st.end_offset(), block.cbor().size());
            expect_equal(st.utxo_balance(), initial_balance + output - fee);
            expect(st.has_stake(stake_ident { key, false }));
            expect(st.has_drep(credential_t { key, false }));
        };

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
