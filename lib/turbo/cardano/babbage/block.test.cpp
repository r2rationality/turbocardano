/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/babbage/block.hpp>
#include <turbo/common/test.hpp>
#include <turbo/plutus/context.hpp>

using namespace turbo;
using namespace turbo::cardano;

suite cardano_babbage_suite = [] {
    "cardano::babbage"_test = [] {
        "parse block header"_test = [] {
            const auto data = file::read(install_path("data/babbage/block-0.cbor"));
            auto block_tuple = cbor::zero2::parse(data);
            auto &it = block_tuple.get().array();
            const babbage::block blk { it.read().uint(), 0, 2, it.read(), cardano::config::get() };
            expect_equal(block_hash::from_hex("CE9A936F64E474A2A0D7543BB36D28DF6153D93F85A81A7A6FB60ECCE8E5D311"), blk.hash());
            expect_equal(block_hash::from_hex("1EA7AEDF01380467C953A672DA92F050EFA6B678E626668650FA980A2AEFEF5C"), blk.prev_hash());
            expect_equal(103831217, blk.slot());
            expect_equal(6, blk.era());
            expect_equal(protocol_version { 8, 0 }, blk.protocol_ver());
            expect(blk.body_hash_ok());
            expect(blk.signature_ok());
        };

        "script_refs in tx_outputs"_test = [] {
            const auto data = file::read(install_path("data/babbage/block-1.cbor"));
            auto block_tuple = cbor::zero2::parse(data);
            auto &it = block_tuple.get().array();
            const babbage::block blk { it.read().uint(), 0, 2, it.read(), cardano::config::get() };
            expect_equal(block_hash::from_hex("11B88315F16B71453942AAE30A4F314AA4BDC8FB6DD54942A7D332F3A5391272"), blk.hash());
            expect_equal(block_hash::from_hex("F900E8B4D1AE294FA8C008412AA4B2FCD78FDD6B70945FDD43E98846B734F534"), blk.prev_hash());
            expect_equal(72391881, blk.slot());
            expect_equal(6, blk.era());
            expect_equal(protocol_version { 7, 0 }, blk.protocol_ver());
            expect(blk.body_hash_ok());
            expect(blk.signature_ok());
            // check that txs reports all transactions including invalid
            expect_equal(28, blk.txs().size());
            size_t num_script_refs = 0;
            blk.foreach_tx([&](const auto &tx) {
                for (const auto &txo: tx.outputs()) {
                    if (txo.script_ref)
                        ++num_script_refs;
                }
            });
            expect_equal(1, num_script_refs);
        };

        "plutus_cost_model param_update #1"_test = [] {
            const auto data = file::read(install_path("data/babbage/block-2.cbor"));
            auto block_tuple = cbor::zero2::parse(data);
            auto &it = block_tuple.get().array();
            const babbage::block blk { it.read().uint(), 0, 2, it.read(), cardano::config::get() };
            expect_equal(block_hash::from_hex("8D4EB9C1E090F3ED4F23FF3690A4ACE2FB474CBFEC15C420C88D9C3FE8FE3823"), blk.hash());
            expect_equal(block_hash::from_hex("1A4A630F8085B70E165C82743BB65C8AE612CC7AE36F5D9D295EC32D73725A3F"), blk.prev_hash());
            expect_equal(72356855, blk.slot());
            expect_equal(6, blk.era());
            expect_equal(protocol_version { 7, 0 }, blk.protocol_ver());
            expect(blk.body_hash_ok());
            expect(blk.signature_ok());
            size_t num_cost_model_props = 0;
            blk.foreach_update_proposal([&](const auto &prop) {
                if (prop.update.plutus_cost_models)
                    ++num_cost_model_props;
            });
            expect_equal(2, num_cost_model_props);
        };

        "plutus_cost_model param_update #2"_test = [] {
            const auto data = file::read(install_path("data/babbage/block-3.cbor"));
            auto block_tuple = cbor::zero2::parse(data);
            auto &it = block_tuple.get().array();
            const babbage::block blk { it.read().uint(), 0, 2, it.read(), cardano::config::get() };
            expect_equal(block_hash::from_hex("BA754FEE79FA4F17890E81B31DE205C52E7C092D4C8AF385DA6F9F87401CEEFB"), blk.hash());
            expect_equal(block_hash::from_hex("4DC2990D5907C47FD68C12A82335A1D74973BD138B2C0EB8B90831DA67896656"), blk.prev_hash());
            expect_equal(133362854, blk.slot());
            expect_equal(6, blk.era());
            expect_equal(protocol_version { 9, 1 }, blk.protocol_ver());
            expect(blk.body_hash_ok());
            expect(blk.signature_ok());
            size_t num_cost_model_props = 0;
            blk.foreach_update_proposal([&](const auto &prop) {
                if (prop.update.plutus_cost_models)
                    ++num_cost_model_props;
            });
            expect_equal(2, num_cost_model_props);
        };

        "example scripts"_test = [] {
            const configs_dir cfg { configs_dir::default_path() };
            const cardano::config ccfg { cfg };
            ccfg.shelley_start_epoch(208);
            for (const auto &path: file::files_with_ext(install_path("data/babbage"), ".zpp")) {
                plutus::context ctx { path, ccfg };
                ctx.protocol_ver({ 8, 0 });
                ctx.prepare();
                expect(boost::ut::nothrow([&] {
                    try {
                        for (const auto &[rid, rinfo]: ctx.redeemers()) {
                            auto ps = ctx.prepare_script(rinfo);
                            ctx.eval_script(ps);
                        }
                    } catch (const error &ex) {
                        logger::warn("context {} failed with {}", path, ex.what());
                        throw;
                    }
                })) << path;
            }
        };

        "body_hash_ok"_test = [] {
            auto chunk = zstd::read("./data/chunk-registry/compressed/chunk/977E9BB3D15A5CFF5C5E48617288C5A731DB654C0B42D63627C690CEADC9E1F3.zstd");
            cbor::zero2::decoder dec { chunk };
            while (!dec.done()) {
                auto &block_tuple = dec.read();
                auto &it = block_tuple.array();
                const babbage::block blk { it.read().uint(), 0, 2, it.read(), cardano::config::get() };
                expect_equal(6, blk.era());
                expect(blk.body_hash_ok());
                expect(blk.signature_ok());
            }
        };
    };
};
