/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/common/file.hpp>
#include <dt/common/test.hpp>
#include <dt/cardano/mary/block.hpp>

using namespace daedalus_turbo;
using namespace daedalus_turbo::cardano;

suite cardano_mary_suite = [] {
    "cardano::mary"_test = [] {
        "parse allegra block"_test = [] {
            const auto data = file::read(install_path("data/allegra/block-0.cbor"));
            auto block_tuple = cbor::zero2::parse(data);
            auto &it = block_tuple.get().array();
            const mary::block blk { it.read().uint(), 0, 2, it.read(), cardano::config::get() };
            test_same(block_hash::from_hex("D8525B55D0E01A54B4FCB740BADB40CE6544301B6277BFB7C262BF33646F7C98"), blk.hash());
            test_same(block_hash::from_hex("7F225EBD16F08E9260204A4B957F07BEC5AA2E3E27AB913BC3CFC5048289FCFA"), blk.prev_hash());
            test_same(18295226, blk.slot());
            test_same(3, blk.era());
            test_same(protocol_version { 4, 0 }, blk.protocol_ver());
            expect(blk.body_hash_ok());
            expect(blk.signature_ok());
        };

        "parse mary block 0"_test = [] {
            const auto data = file::read(install_path("data/mary/block-0.cbor"));
            auto block_tuple = cbor::zero2::parse(data);
            auto &it = block_tuple.get().array();
            const mary::block blk { it.read().uint(), 0, 2, it.read(), cardano::config::get() };
            test_same(block_hash::from_hex("AE45351BE98AFB083F05B9A1F57F9632B0DDEF698D92BA6D36A6A4E54E8D7D2E"), blk.hash());
            test_same(block_hash::from_hex("40FAEED7CCCEBDDF4BE9C2F2E15D366FEFD27CFE2F0C5F8A660430FB130D57E1"), blk.prev_hash());
            test_same(26935217, blk.slot());
            test_same(4, blk.era());
            test_same(protocol_version { 4, 0 }, blk.protocol_ver());
            expect(blk.body_hash_ok());
            expect(blk.signature_ok());
        };

        "parse mary block 1"_test = [] {
            const auto data = file::read(install_path("data/mary/block-1.cbor"));
            auto block_tuple = cbor::zero2::parse(data);
            auto &it = block_tuple.get().array();
            const mary::block blk { it.read().uint(), 0, 2, it.read(), cardano::config::get() };
            test_same(block_hash::from_hex("52FD6283BC5A1E6B78707A1534ABFB3AA2499CA2C108741D5DB6C8C7D99353AE"), blk.hash());
            test_same(block_hash::from_hex("08ECDB54A80C81073DDEE790F3CF4F8D4FF4422EB369B41CD5E0E18C13DE6BBE"), blk.prev_hash());
            test_same(26250031, blk.slot());
            test_same(4, blk.era());
            test_same(protocol_version { 4, 0 }, blk.protocol_ver());
            expect(blk.body_hash_ok());
            expect(blk.signature_ok());
        };

        "body_hash_ok"_test = [] {
            auto chunk = zstd::read("./data/chunk-registry/compressed/chunk/7C46426DDF73FFFAD5970B0F1C0983A3A98F5AC3EC080BDFB59DBF86AC1AE9A1.zstd");
            cbor::zero2::decoder dec { chunk };
            while (!dec.done()) {
                auto &block_tuple = dec.read();
                auto &it = block_tuple.array();
                const mary::block blk { it.read().uint(), 0, 2, it.read(), cardano::config::get() };
                test_same(4, blk.era());
                expect(blk.body_hash_ok());
                expect(blk.signature_ok());
            }
        };
    };
};