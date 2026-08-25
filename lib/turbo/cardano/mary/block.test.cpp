/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/mary/block.hpp>
#include <turbo/common/file.hpp>
#include <turbo/common/test.hpp>

using namespace turbo;
using namespace turbo::cardano;

suite cardano_mary_suite = [] {
    "cardano::mary"_test = [] {
        "mint parsing prunes zero quantities"_test = [] {
            // The first policy has one zero and one negative asset. The second policy
            // contains only a zero asset and must therefore disappear altogether.
            const auto raw = uint8_vector::from_hex(
                "A2"
                "581C00000000000000000000000000000000000000000000000000000000"
                "A241A00041A121"
                "581C01010101010101010101010101010101010101010101010101010101"
                "A14000");
            auto parsed = cbor::zero2::parse(raw);
            const auto mints = mary::mint_t::from_cbor(parsed.get());

            expect_equal(1, mints.size());
            expect_equal(1, mints.begin()->second.size());
            expect_equal(-2, mints.begin()->second.begin()->second);
        };

        "parse allegra block"_test = [] {
            const auto data = file::read(install_path("data/allegra/block-0.cbor"));
            auto block_tuple = cbor::zero2::parse(data);
            auto &it = block_tuple.get().array();
            const mary::block blk { it.read().uint(), 0, 2, it.read(), cardano::config::get() };
            expect_equal(block_hash::from_hex("D8525B55D0E01A54B4FCB740BADB40CE6544301B6277BFB7C262BF33646F7C98"), blk.hash());
            expect_equal(block_hash::from_hex("7F225EBD16F08E9260204A4B957F07BEC5AA2E3E27AB913BC3CFC5048289FCFA"), blk.prev_hash());
            expect_equal(18295226, blk.slot());
            expect_equal(3, blk.era());
            expect_equal(protocol_version { 4, 0 }, blk.protocol_ver());
            expect(blk.body_hash_ok());
            expect(blk.signature_ok());
        };

        "parse mary block 0"_test = [] {
            const auto data = file::read(install_path("data/mary/block-0.cbor"));
            auto block_tuple = cbor::zero2::parse(data);
            auto &it = block_tuple.get().array();
            const mary::block blk { it.read().uint(), 0, 2, it.read(), cardano::config::get() };
            expect_equal(block_hash::from_hex("AE45351BE98AFB083F05B9A1F57F9632B0DDEF698D92BA6D36A6A4E54E8D7D2E"), blk.hash());
            expect_equal(block_hash::from_hex("40FAEED7CCCEBDDF4BE9C2F2E15D366FEFD27CFE2F0C5F8A660430FB130D57E1"), blk.prev_hash());
            expect_equal(26935217, blk.slot());
            expect_equal(4, blk.era());
            expect_equal(protocol_version { 4, 0 }, blk.protocol_ver());
            expect(blk.body_hash_ok());
            expect(blk.signature_ok());
        };

        "parse mary block 1"_test = [] {
            const auto data = file::read(install_path("data/mary/block-1.cbor"));
            auto block_tuple = cbor::zero2::parse(data);
            auto &it = block_tuple.get().array();
            const mary::block blk { it.read().uint(), 0, 2, it.read(), cardano::config::get() };
            expect_equal(block_hash::from_hex("52FD6283BC5A1E6B78707A1534ABFB3AA2499CA2C108741D5DB6C8C7D99353AE"), blk.hash());
            expect_equal(block_hash::from_hex("08ECDB54A80C81073DDEE790F3CF4F8D4FF4422EB369B41CD5E0E18C13DE6BBE"), blk.prev_hash());
            expect_equal(26250031, blk.slot());
            expect_equal(4, blk.era());
            expect_equal(protocol_version { 4, 0 }, blk.protocol_ver());
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
                expect_equal(4, blk.era());
                expect(blk.body_hash_ok());
                expect(blk.signature_ok());
            }
        };
    };
};
