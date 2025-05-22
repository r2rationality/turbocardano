/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/common/test.hpp>
#include <dt/cardano/common/block-producer.hpp>
#include <dt/cardano.hpp>

namespace {
    using namespace daedalus_turbo;
    using namespace daedalus_turbo::cardano;
}

suite cardano_block_producer_suite = [] {
    using boost::ext::ut::v2_1_0::nothrow;
    "cardano::block_producer"_test = [] {
        "one empty block"_test = [] {
            ed25519::skey cold_sk {};
            ed25519::vkey cold_vk {};
            ed25519::create(cold_sk, cold_vk);
            auto seed = blake2b<ed25519::seed>(std::string_view { "1" });
            const auto vrf_sk = vrf03_create_sk_from_seed(seed);
            block_producer bp { cold_sk, seed, vrf_sk };
            const auto raw_data = bp.cbor();
            expect(raw_data.size() >= 512);
            auto block_tuple = cbor::zero2::parse(raw_data);
            const block_container blk { 213, block_tuple.get() };
            test_same(6, blk->era());
            test_same(213, blk.offset());
            test_same(0, blk->height());
            test_same(0, blk->slot());
            expect(blk->signature_ok());
        };
        "two producers"_test = [] {
            uint8_vector chain {};

            auto seed1 = blake2b<ed25519::seed>(std::string_view { "1" });
            auto sk1 = ed25519::create_sk_from_seed(seed1);
            const auto vrf_sk1 = vrf03_create_sk_from_seed(seed1);
            block_producer bp1 { sk1, seed1, vrf_sk1 };

            auto seed2 = blake2b<ed25519::seed>(std::string_view { "2" });
            auto sk2 = ed25519::create_sk_from_seed(seed2);
            const auto vrf_sk2 = vrf03_create_sk_from_seed(seed1);
            block_producer bp2 { sk2, seed2, vrf_sk2 };

            block_hash prev_hash {};
            uint64_t height = 0;
            for (const uint64_t slot: { 0, 13, 44, 57 }) {
                auto &bp = slot % 2 == 0 ? bp1 : bp2;
                bp.height = height;
                bp.slot = slot;
                bp.prev_hash = prev_hash;
                const auto block_data = bp.cbor();
                auto block_tuple = cbor::zero2::parse(block_data);
                const block_container blk { chain.size(), block_tuple.get() };
                expect(blk->era() == 6_ull);
                expect(blk.offset() == chain.size());
                expect(blk->height() == height);
                expect(blk->slot() == slot);
                expect(blk->prev_hash() == prev_hash);
                expect(blk->signature_ok());
                chain << block_data;
                prev_hash = blk->hash();
                ++height;
            }
        };
        "transactions"_test = [] {
            const auto [cold_sk, cold_vk] = ed25519::create_from_seed(blake2b<ed25519::seed>(std::string_view { "cold-1" }));
            const auto vrf_seed = blake2b<ed25519::seed>(std::string_view { "vrf-1" });
            const auto vrf_sk = vrf03_create_sk_from_seed(vrf_seed);
            block_producer bp { cold_sk, vrf_seed, vrf_sk };

            const auto [acc1_sk, acc1_vk] = ed25519::create_from_seed(blake2b<ed25519::seed>(std::string_view { "acc-1" }));
            const auto [acc2_sk, acc2_vk] = ed25519::create_from_seed(blake2b<ed25519::seed>(std::string_view { "acc-2" }));
            block_producer::tx_input in1 { tx_hash::from_hex("0000000000000000000000000000000000000000000000000000000000000000"), 1, acc1_sk, acc1_vk };
            block_producer::tx_input in2 { tx_hash::from_hex("1111111111111111111111111111111111111111111111111111111111111111"), 2, acc1_sk, acc1_vk };
            block_producer::tx_input in3 { tx_hash::from_hex("2222222222222222222222222222222222222222222222222222222222222222"), 0, acc2_sk, acc2_vk };
            block_producer::tx_output out1 { uint8_vector::from_hex("010000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"), 1234567 };
            block_producer::tx_output out2 { uint8_vector::from_hex("011111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111"), 987654321 };
            bp.txs.emplace_back(
                block_producer::tx_input_list { in1, in2, in3 },
                block_producer::tx_output_list { out1, out2 }
            );
            const auto raw_data = bp.cbor();
            expect(raw_data.size() >= 512);
            auto block_tuple = cbor::zero2::parse(raw_data);
            const block_container blk { 123, block_tuple.get() };
            test_same(6, blk->era());
            test_same(123, blk.offset());
            test_same(0, blk->height());
            test_same(0, blk->slot());
            test_same(1, blk->tx_count());
            blk->foreach_tx([&](const auto &tx) {
                expect(nothrow([&]{ tx.witnesses_ok(); }));
                size_t txi_idx = 0;
                tx.foreach_input([&](const auto &txi) {
                    const auto &exp_txi = bp.txs.at(tx.index()).inputs.at(txi_idx++);
                    test_same(exp_txi.tx_hash, txi.hash);
                    test_same(exp_txi.txo_idx, txi.idx);
                });
                size_t txo_idx = 0;
                tx.foreach_output([&](const auto &txo) {
                    const auto &exp_tx_out = bp.txs.at(tx.index()).outputs.at(txo_idx++);
                    test_same(exp_tx_out.address, txo.address_raw);
                    test_same(exp_tx_out.coin, txo.coin);
                });
            });
            expect(blk->signature_ok());
        };
    };
};