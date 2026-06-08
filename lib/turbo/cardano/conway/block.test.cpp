/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cardano/conway/block.hpp>
#include <turbo/common/test.hpp>

using namespace turbo;
using namespace turbo::cardano;

suite cardano_conway_suite = [] {
    "cardano::conway"_test = [] {
        "example scripts"_test = [] {
            const configs_dir cfg { configs_dir::default_path() };
            const cardano::config ccfg { cfg };
            ccfg.shelley_start_epoch(208);
            for (const auto &path: file::files_with_ext(install_path("data/conway"), ".zpp")) {
                const plutus::context ctx { path, ccfg };
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

        "parse block header"_test = [] {
            const auto data = file::read(install_path("data/conway/block-0.cbor"));
            auto block_tuple = cbor::zero2::parse(data);
            auto &it = block_tuple.get().array();
            const conway::block blk { it.read().uint(), 0, 2, it.read(), cardano::config::get() };
            expect_equal(block_hash::from_hex("D01634A1BA819AF2908F91CF33F77501C2DC7660A0B751619477AF3B3F75F7E6"), blk.hash());
            expect_equal(block_hash::from_hex("88A963E59E042A2109D0B6FB8EC12D0AC9EB843C9EA1F9A76FFE84A516E4AF0C"), blk.prev_hash());
            expect_equal(139255207, blk.slot());
            expect_equal(7, blk.era());
            expect_equal(protocol_version { 9, 1 }, blk.protocol_ver());
            expect(blk.body_hash_ok());
        };

        "pool_params"_test = [] {
            const auto data = file::read(install_path("data/conway/block-1.cbor"));
            auto block_tuple = cbor::zero2::parse(data);
            auto &it = block_tuple.get().array();
            const conway::block blk { it.read().uint(), 0, 2, it.read(), cardano::config::get() };
            expect_equal(block_hash::from_hex("A41A12462EA28DF2FC7488F504CA2DB73027514538CA39E9A74CABAFF5FC1578"), blk.hash());
            expect_equal(block_hash::from_hex("E6592BB6CAD8D7F74831F6452FD5E9DB46A17C25906FDE4B633BB567C8111690"), blk.prev_hash());
            expect_equal(133878129, blk.slot());
            expect_equal(7, blk.era());
            expect_equal(protocol_version { 9, 1 }, blk.protocol_ver());
            expect(blk.body_hash_ok());
            expect(blk.signature_ok());
            // check that txs reports all transactions including invalid
            expect_equal(8, blk.txs().size());
            size_t num_certs = 0;
            blk.foreach_tx([&](const auto &tx) {
                num_certs += tx.certs().size();
            });
            expect_equal(1, num_certs);
        };

        "plutus_cost_models"_test = [] {
            const auto data = file::read(install_path("data/conway/block-2.cbor"));
            auto block_tuple = cbor::zero2::parse(data);
            auto &it = block_tuple.get().array();
            const conway::block blk { it.read().uint(), 0, 2, it.read(), cardano::config::get() };
            expect_equal(block_hash::from_hex("64AC517915DEC4255F990C29C70B4E414D7F26701BBB6B668B47FF5EB3958298"), blk.hash());
            expect_equal(block_hash::from_hex("A72E59D492D7D364B7CFEE2C93A379E1B7936CA609B3C37544D659CD14CF40C9"), blk.prev_hash());
            expect_equal(138832413, blk.slot());
            expect_equal(7, blk.era());
            expect_equal(protocol_version { 9, 1 }, blk.protocol_ver());
            expect(blk.body_hash_ok());
            expect(blk.signature_ok());
            size_t new_v3_cost_models = 0;
            for (const auto &tx: blk.txs()) {
                const auto &c_tx = dynamic_cast<const conway::tx &>(*tx);
                for (const auto &p: c_tx.proposals()) {
                    if (std::holds_alternative<gov_action_t::parameter_change_t>(p.procedure.action.val)) {
                        const auto &pupd = std::get<gov_action_t::parameter_change_t>(p.procedure.action.val);
                        if (pupd.update.plutus_cost_models && pupd.update.plutus_cost_models->contains(2))
                            ++new_v3_cost_models;
                    }
                }
            }
            expect_equal(1, new_v3_cost_models);
        };

        "votes"_test = [] {
            const auto data = file::read(install_path("data/conway/block-3.cbor"));
            auto block_tuple = cbor::zero2::parse(data);
            auto &it = block_tuple.get().array();
            const conway::block blk { it.read().uint(), 0, 2, it.read(), cardano::config::get() };
            expect_equal(block_hash::from_hex("1CA6C5F875E8BC616C6825568CB2B79BE45FC16D3D7B8B57EEF1AFA0A9EFBEC6"), blk.hash());
            expect_equal(block_hash::from_hex("6C3A2AED8075BBEAF5C55BD1368182C1C68235AA9BAF3B00E15FE82A6176A052"), blk.prev_hash());
            expect_equal(146179467, blk.slot());
            expect_equal(7, blk.era());
            expect_equal(protocol_version { 10, 2 }, blk.protocol_ver());
            expect(blk.body_hash_ok());
            expect(blk.signature_ok());
            size_t num_votes = 0;
            for (const auto &tx: blk.txs()) {
                if (const auto *c_tx = dynamic_cast<const cardano::conway::tx *>(tx); c_tx) {
                    num_votes += c_tx->votes().size();
                }
            }
            expect_equal(1, num_votes);
        };
    };
};
