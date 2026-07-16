/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/shelley/block.hpp>
#include <turbo/common/file.hpp>
#include <turbo/common/test.hpp>
#include <turbo/crypto/sha3.hpp>

using namespace turbo;
using namespace turbo::cardano;

suite cardano_sheley_suite = [] {
    "cardano::shelley"_test = [] {
        "parse block header"_test = [] {
            const auto data = file::read(install_path("data/shelley/block-0.cbor"));
            auto block_tuple = cbor::zero2::parse(data);
            auto &it = block_tuple.get().array();
            const shelley::block blk { it.read().uint(), 0, 2, it.read(), cardano::config::get() };
            expect_equal(block_hash::from_hex("AA83ACBF5904C0EDFE4D79B3689D3D00FCFC553CF360FD2229B98D464C28E9DE"), blk.hash());
            expect_equal(block_hash::from_hex("F8084C61B6A238ACEC985B59310B6ECEC49C0AB8352249AFD7268DA5CFF2A457"), blk.prev_hash());
            expect_equal(4492800, blk.slot());
            expect_equal(2, blk.era());
            expect_equal(protocol_version { 2, 0 }, blk.protocol_ver());
            expect(blk.body_hash_ok());
            expect(blk.signature_ok());
        };

        "body_hash_ok"_test = [] {
            const auto chunk = zstd::read("./data/chunk-registry/compressed/chunk/DF597E3FA352A7BD2F021733804C33729EBAA3DCAA9C0643BD263EFA09497B03.zstd");
            cbor::zero2::decoder dec { chunk };
            while (!dec.done()) {
                auto &block_tuple = dec.read();
                auto &it = block_tuple.array();
                const shelley::block blk { it.read().uint(), 0, 2, it.read(), cardano::config::get() };
                expect_equal(2, blk.era());
                expect(blk.body_hash_ok());
                expect(blk.signature_ok());
            }
        };

        "bootstrap hash"_test = [] {
            const auto vk = vkey::from_hex("f202012360fa94af83651a8b8b9592bcda2bee5e187c40d4263a838107c27ae8");
            const auto cc = vkey::from_hex("A1BBC30CF781C0A81B1AFC059B7362111F70C45409CA71FC9E165A78E9C97896");
            const auto attrs = uint8_vector::from_hex("A101581E581C8D9B2A782A9B394EFF8857682BCD29B048835B93F9A2EDF33EDE73DD");
            const auto exp_bytes = uint8_vector::from_hex("830082005840F202012360FA94AF83651A8B8B9592BCDA2BEE5E187C40D4263A838107C27AE8A1BBC30CF781C0A81B1AFC059B7362111F70C45409CA71FC9E165A78E9C97896A101581E581C8D9B2A782A9B394EFF8857682BCD29B048835B93F9A2EDF33EDE73DD");
            const auto exp_root = key_hash::from_hex("617eb950868cacc3ced1f8b84984d52983ae2dd3d829104564317401");
            {
                cbor::encoder enc {};
                enc.array(3);
                enc.uint(0);
                enc.array(2);
                enc.uint(0);
                uint8_vector vk_full {};
                vk_full << vk << cc;
                enc.bytes(vk_full);
                enc.raw_cbor(attrs);
                expect_equal(exp_bytes, enc.cbor());
                expect_equal(exp_root, crypto::blake2b::digest<key_hash>(crypto::sha3::digest(enc.cbor())));
            }

            {
                const auto addr_raw = uint8_vector::from_hex("83581C617EB950868CACC3CED1F8B84984D52983AE2DD3D829104564317401A101581E581C8D9B2A782A9B394EFF8857682BCD29B048835B93F9A2EDF33EDE73DD00");
                const auto b_addr = byron_addr::from_bytes(addr_raw);
                cbor::encoder enc {};
                enc.array(4);
                enc.bytes(vk);
                enc.s_null();
                enc.bytes(cc);
                enc.bytes(attrs);
                expect_equal(true, b_addr.bootstrap_ok(cbor::zero2::parse(enc.cbor()).get()));
            }
        };
        "bootstrap witness"_test = [] {
            struct test_vec {
                const char *addr;
                const char *wit;
            };
            static std::vector<test_vec> test_vecs {
                {
                    "83581C617EB950868CACC3CED1F8B84984D52983AE2DD3D829104564317401A101581E581C8D9B2A782A9B394EFF8857682BCD29B048835B93F9A2EDF33EDE73DD00",
                    "845820F202012360FA94AF83651A8B8B9592BCDA2BEE5E187C40D4263A838107C27AE858401C19005D3966C6D3734BDE29EC60DB8A493C89CB37A2BCBD569EFAADD623B28F396DE340F40F08B4660DFA728D0CC6CCAB627324AD854F1B6B2D16854F1EAB095820A1BBC30CF781C0A81B1AFC059B7362111F70C45409CA71FC9E165A78E9C978965822A101581E581C8D9B2A782A9B394EFF8857682BCD29B048835B93F9A2EDF33EDE73DD",
                },
                {
                    "83581C74D8516818C8FBC8087FC1CBDD7532474CB8C84411155FB0E964ABB1A101581E581C8D9B2A782A9B390866FEBB68228DD65728C218E7FD86142C4D2736B300",
                    "84582054E11B8CC512CE2B45B1B053E8F0E87BA9157A796F5653C20DDFEF5AF89A18E458406E2B18DDE52DEABB476AE171EC56B3620C63C0D6A00069F3BD7160CA0AF7DD923CBF1FF1B414CB88D824B843B97FCE5DF70B477FAD1FAFD62AA1A2556E13F207582052FBFA9E0ACA2281D1EF0359F9880B381E8B045273C59C9827F9762E939E768C5822A101581E581C8D9B2A782A9B390866FEBB68228DD65728C218E7FD86142C4D2736B3",
                },
                {
                    "83581CE465512206311176A1F56B81737E68E338BE12C0D0F6C837246E98EDA101581E581C8D9B2A782A9B392EF1C4A1683CFF72B7B6A02CD5DCA8AFB5C1A1597300",
                    "845820F5C382061F453618C6C957BB5345860B2736FEE4FC29133215134FD92A26D03F5840D26BB37EF4EFC404C59865B4F76D51D76C2C3D659430E74F7566884D1CFFE5061DA326BE3AAF54DFDDB9E89D69E31289DBEE63DABBC5A71EA279BF7B10F8300758203AB2C5F5262FF16DE9FE44821853B266D599906090662C40785355931D965F0C5822A101581E581C8D9B2A782A9B392EF1C4A1683CFF72B7B6A02CD5DCA8AFB5C1A15973",
                }
            };
            for (size_t i = 0; i < test_vecs.size(); ++i) {
                const auto &test = test_vecs[i];
                const auto wit_raw = uint8_vector::from_hex(test.wit);
                auto wit = cbor::zero2::parse(wit_raw);
                const auto addr_raw = uint8_vector::from_hex(test.addr);
                const auto addr = byron_addr::from_bytes(addr_raw);
                expect_equal(true, addr.bootstrap_ok(wit.get()), fmt::format("test vector #{}: {}", i, test.addr));
            }
        };
    };
};