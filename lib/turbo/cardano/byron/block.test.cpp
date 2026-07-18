/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/byron/block.hpp>
#include <turbo/common/file.hpp>
#include <turbo/common/test.hpp>
#include <turbo/plutus/context.hpp>

namespace {
    using namespace std::literals::string_view_literals;
    using namespace turbo;
    using namespace turbo::cardano;
}

suite cardano_byron_suite = [] {
    "cardano::byron"_test = [] {
        "custom txs"_test = [] {
            const configs_dir cfg { configs_dir::default_path() };
            const cardano::config ccfg { cfg };
            for (const auto &path: file::files_with_ext(install_path("data/byron"), ".zpp")) {
                const plutus::context ctx { path, ccfg };
                const auto wit_cnts = ctx.tx().witnesses_ok();
                expect_equal(true, wit_cnts);
            }
        };

        "boundary block"_test = [] {
            const auto data = file::read(install_path("data/byron/block-0.cbor"));
            auto block_tuple = cbor::zero2::parse(data);
            auto &it = block_tuple.get().array();
            const byron::boundary_block blk { it.read().uint(), 0, 2, it.read(), cardano::config::get() };
            expect_equal(block_hash::from_hex("89D9B5A5B8DDC8D7E5A6795E9774D97FAF1EFEA59B2CAF7EAF9F8C5B32059DF4"), blk.hash());
            expect_equal(block_hash::from_hex("5F20DF933584822601F9E3F8C024EB5EB252FE8CEFB24D1317DC3D432E940EBB"), blk.prev_hash());
            expect_equal(0, blk.slot());
            expect_equal(0, blk.era());
            expect_equal(0, blk.height());
            expect_equal(80, blk.header().data_raw().size());
            expect_equal(protocol_version { 1, 0 }, blk.protocol_ver());
            expect_equal(uint8_vector::from_hex("0000000000000000000000000000000000000000000000000000000000000000"), blk.issuer_vkey());
            expect_equal(0, blk.body_size());
            expect_equal(0, blk.txs().size());
            expect_equal(true, blk.signature_ok());

        };

        "regular block"_test = [] {
            const auto data = file::read(install_path("data/byron/block-1.cbor"));
            auto block_tuple = cbor::zero2::parse(data);
            auto &it = block_tuple.get().array();
            const byron::block blk { it.read().uint(), 0, 2, it.read(), cardano::config::get() };
            expect_equal(block_hash::from_hex("F0F7892B5C333CFFC4B3C4344DE48AF4CC63F55E44936196F365A9EF2244134F"), blk.hash());
            expect_equal(block_hash::from_hex("89D9B5A5B8DDC8D7E5A6795E9774D97FAF1EFEA59B2CAF7EAF9F8C5B32059DF4"), blk.prev_hash());
            expect_equal(0, blk.slot());
            expect_equal(1, blk.era());
            expect_equal(0, blk.height());
            expect_equal(uint8_vector::from_hex("1A2D964A09"), dynamic_cast<const byron::block_header &>(blk.header()).protocol_magic_raw());
            expect(blk.header().data_raw().size() < block_tuple.get().data_raw().size());
            expect_equal(protocol_version { 1, 0 }, blk.protocol_ver());
            // byron signed data is lazily computed so evaluate signature_ok twice
            expect(blk.signature_ok());
            expect(blk.signature_ok());
            expect(blk.body_hash_ok());
        };

        "block with an update proposal"_test = [] {
            const auto data = file::read(install_path("data/byron/block-73486.cbor"));
            auto block_tuple = cbor::zero2::parse(data);
            auto &it = block_tuple.get().array();
            const byron::block blk { it.read().uint(), 0, 2, it.read(), cardano::config::get() };
            expect_equal(block_hash::from_hex("9F63162B78765332C1632F46960FB386DA7A2916090E2EF155AB97941EE1BBB9"), blk.hash());
            expect_equal(block_hash::from_hex("1D1C6F61DB4509A5978D54BD4DD1BAD192C4B56086C9B6E419C9B1EFA2141913"), blk.prev_hash());
            expect_equal(73486, blk.slot());
            expect_equal(1, blk.era());
            expect_equal(protocol_version { 1, 0 }, blk.protocol_ver());
            expect(blk.body_hash_ok());
            {
                size_t num_props = 0;
                blk.foreach_update_proposal([&](const auto &) {
                    ++num_props;
                });
                expect_equal(1, num_props);
            }
            {
                size_t num_votes = 0;
                blk.foreach_update_vote([&](const auto &) {
                    ++num_votes;
                });
                expect_equal(1, num_votes);
            }
        };

        "block with non-empty transactions"_test = [] {
            const auto data = file::read(install_path("data/byron/block-4320007.cbor"));
            auto block_tuple = cbor::zero2::parse(data);
            auto &it = block_tuple.get().array();
            const byron::block blk { it.read().uint(), 0, 2, it.read(), cardano::config::get() };
            expect_equal(block_hash::from_hex("65D56B61CD0C904D6DF023AC2EB687D4D7E75150C72740220665377C16495128"), blk.hash());
            expect_equal(block_hash::from_hex("55680587904109B2CE2BA3C1989CD58DB2CDCEA89F30106759E88C2E7D1630B6"), blk.prev_hash());
            expect_equal(4320007, blk.slot());
            expect_equal(1, blk.era());
            expect_equal(protocol_version { 1, 0 }, blk.protocol_ver());
            expect(blk.body_hash_ok());
            expect_equal(4, blk.txs().size());
            for (const auto &tx: blk.txs()) {
                expect(throws([&]{ tx->fee(); }));
                expect(throws([&]{ tx->inputs(); }));
                expect(!tx->outputs().empty());
            }
        };

        "genesis signers"_test = [] {
            const std::set<vkey> signers = {
                vkey::from_hex("5FDDEEDADE2714D6DB2F9E1104743D2D8D818ECDDC306E176108DB14CAADD441"),
                vkey::from_hex("61261A95B7613EE6BF2067DAD77B70349729B0C50D57BC1CF30DE0DB4A1E73A8"),
                vkey::from_hex("89C29F8C4AF27B7ACCBE589747820134EBBAA1CAF3CE949270A3D0C7DCFD541B"),
                vkey::from_hex("8B53207629F9A30E4B2015044F337C01735ABE67243C19470C9DAE8C7B732798"),
                vkey::from_hex("9180D818E69CD997E34663C418A648C076F2E19CD4194E486E159D8580BC6CDA"),
                vkey::from_hex("E8C03A03C0B2DDBEA4195CAF39F41E669F7D251ECF221FBB2F275C0A5D7E05D1"),
                vkey::from_hex("F14F712DC600D793052D4842D50CEFA4E65884EA6CF83707079EB8CE302EFC85")
            };
            const auto &cfg = cardano::config::get();
            for (const auto &vk: signers) {
                expect(cfg.byron_delegate_hashes.contains(crypto::blake2b::digest<key_hash>(vk))) << fmt::format("{}", vk);
            }
        };
        "match slot 1 data"_test = [] {
            // dlgsig.dlg.delegate
            auto cbor_vkey_full = uint8_vector::from_hex("e8c03a03c0b2ddbea4195caf39f41e669f7d251ecf221fbb2f275c0a5d7e05d190dcc246f56c8e33ac0037066e2f664ddaa985ea5284082643308dde4f5bfedf");
            auto cbor_vkey = buffer(cbor_vkey_full.data(), 32);
            // dlgsig.signature
            auto cbor_sig = uint8_vector::from_hex("923c7714af7fe4b1272fc042111ece6fd08f5f16298d62bae755c70c1e1605697cbaed500e196330f40813128250d9ede9c8557b33f48e8a5f32f765929e4a0d");
            auto trace_vkey = from_haskell("\\232\\192:\\ETX\\192\\178\\221\\190\\164\\EM\\\\\\175\\&9\\244\\RSf\\159}%\\RS\\207\\\"\\US\\187/'\\\\\\n]~\\ENQ\\209"sv);
            auto trace_sig = from_haskell("\\146<w\\DC4\\175\\DEL\\228\\177'/\\192B\\DC1\\RS\\206o\\208\\143_\\SYN)\\141b\\186\\231U\\199\\f\\RS\\SYN\\ENQi|\\186\\237P\\SO\\EMc0\\244\\b\\DC3\\DC2\\130P\\217\\237\\233\\200U{3\\244\\142\\138_2\\247e\\146\\158J\\r"sv);
            expect(cbor_vkey.size() == trace_vkey.size()) << cbor_vkey.size() << "!=" << trace_vkey.size();
            expect(cbor_sig.size() == trace_sig.size()) << cbor_sig.size() << "!=" << trace_sig.size();
            expect(cbor_vkey == trace_vkey) << cbor_vkey << "!=" << trace_vkey;
            expect(cbor_sig == trace_sig) << cbor_sig << "!=" << trace_sig;
        };

        "block signature validation for slot 1"_test = [] {
            auto vkey = from_haskell("\\232\\192:\\ETX\\192\\178\\221\\190\\164\\EM\\\\\\175\\&9\\244\\RSf\\159}%\\RS\\207\\\"\\US\\187/'\\\\\\n]~\\ENQ\\209"sv);
            auto msg = from_haskell("01Ps1a\\253\\175\\182\\200\\203o\\174\\SO%\\189\\249UQ\\ENQ\\179g\\142\\251\\b\\241w[\\158\\144\\222O\\\\w\\188\\200\\206\\255\\248\\217\\SOH\\FS\\178x\\178\\143\\221\\200m\\155\\171\\t\\150V\\215zxV\\199a\\145\\b\\203\\246WR\\t\\SUB-\\150J\\t\\133X \\240\\247\\137+\\\\3<\\255\\196\\179\\196\\&4M\\228\\138\\244\\204c\\245^D\\147a\\150\\243e\\169\\239\\\"D\\DC3O\\132\\131\\NULX \\SOWQ\\192&\\229C\\178\\232\\171.\\176`\\153\\218\\161\\209\\229\\223Gw\\143w\\135\\250\\171E\\205\\241/\\227\\168X \\175\\192\\218d\\CAN;\\242fO=N\\236r8\\213$\\186`\\DEL\\174\\234\\178O\\193\\NUL\\235\\134\\GS\\186i\\151\\ESC\\131\\NULX %wz\\202\\158Js\\212\\143\\199;O\\150\\GS4[\\ACK\\212\\166\\243I\\203y\\SYNW\\r5S}SG\\159X \\211j&\\EM\\166rIF\\EOT\\225\\ESC\\180G\\203\\207R1\\233\\242\\186%\\194\\SYN\\145w\\237\\201A\\189P\\173lX \\175\\192\\218d\\CAN;\\242fO=N\\236r8\\213$\\186`\\DEL\\174\\234\\178O\\193\\NUL\\235\\134\\GS\\186i\\151\\ESCX Nf(\\f\\217MY\\DLEr4\\155\\236\\n0\\144\\165:\\169EV.\\251m\\b\\213nSeK\\SO@\\152\\130\\NUL\\SOH\\129\\STX\\132\\131\\NUL\\NUL\\NUL\\130jcardano-sl\\NUL\\160X K\\169*\\163 \\198\\n\\204\\154\\215\\185\\166O.\\218U\\196\\210\\236(\\230\\EOT\\250\\241\\134p\\139O\\fN\\142\\223"sv);
            auto sig = from_haskell("\\146<w\\DC4\\175\\DEL\\228\\177'/\\192B\\DC1\\RS\\206o\\208\\143_\\SYN)\\141b\\186\\231U\\199\\f\\RS\\SYN\\ENQi|\\186\\237P\\SO\\EMc0\\244\\b\\DC3\\DC2\\130P\\217\\237\\233\\200U{3\\244\\142\\138_2\\247e\\146\\158J\\r"sv);
            expect(vkey.size() == 32);
            expect(sig.size() == 64);
            expect(crypto::ed25519::verify(sig, vkey, msg));
        };

        "validate signature based from block CBOR"_test = [] {
            auto buf = file::read("./data/block-slot-1.bin"sv);
            auto block_tuple = cbor::zero2::parse(buf);
            auto &it = block_tuple.get().array();
            const byron::block blk { it.read().uint(), 0, 2, it.read(), cardano::config::get() };
            expect(blk.body_hash_ok());
            expect(blk.signature_ok());
        };

        "block signature validation - cardano test vector A0"_test = [] {
            auto msg = uint8_vector::from_hex("011a2d964a095820a2dbee7247273125ea1c3b9f140d2ea370c705b55e03c36d8d60c6535920f9c6"sv);
            auto sig = uint8_vector::from_hex("a2fc141c749d04e02a63a7df131d0bb1f47027801e1c22bc76cb9792d29ff0936c02666ac8e38347d02a7bfa13bd6cfada31d76087e754103eaf2e6dbf5d020b"sv);
            auto vkey_full = uint8_vector::from_hex("3dd06d2f961b922192529557eb59ad93fcca5e751a816a34808232852b3decfc27ef927dbccf4c0dc8de6b854871cf7b6d88bf18984cb2fc7c49f39aecb97db0"sv);
            auto vkey = buffer(vkey_full.data(), 32);
            expect(crypto::ed25519::verify(sig, vkey, msg));
        };

        "block signature validation - cardano test vector A1"_test = [] {
            auto msg = uint8_vector::from_hex("011a2d964a095820a2dbee7247273125ea1c3b9f140d2ea370c705b55e03c36d8d60c6535920f9c6"sv);
            auto sig = uint8_vector::from_hex("e02e78a5184ed787160afa1637b6f39f74bbadd4f8cf5eb8ba7e306ab616409fe65b1f893b7784535b26f81bfd4a01cfbcb840664ab1764b3bae420954dadc06"sv);
            auto vkey_full = uint8_vector::from_hex("34651bf3ef0b455f50d0db2a9f6e6446c2c5e2ce56ea796657aa01790ab552d105d5b7b8f544d892d52c46ce820048dec6e68e6e08c55ec566cb1207eb5a1cff"sv);
            auto vkey = buffer(vkey_full.data(), 32);
            expect(crypto::ed25519::verify(sig, vkey, msg));
        };

        "block signature validation - cardano test vector A2"_test = [] {
            auto msg = uint8_vector::from_hex("011a2d964a095820a2dbee7247273125ea1c3b9f140d2ea370c705b55e03c36d8d60c6535920f9c6"sv);
            auto sig = uint8_vector::from_hex("562ea76122fab1b0157c0522f027967765135c381706fc042e58abe4b68c545a8486ed6799d3309de3189481569e378c87ae080e5c94ada19308f167b0520e01"sv);
            auto vkey_full = uint8_vector::from_hex("901326c5174cb72bd58852a6e3d5d5062bfb43a15551bd38335c5e44486a2aaf87aa927b7e117d9729284c6885cdd378e2613bb0b4362e837aa399a34d384bd6"sv);
            auto vkey = buffer(vkey_full.data(), 32);
            expect(crypto::ed25519::verify(sig, vkey, msg));
        };

        "block signature validation - cardano test vector A3"_test = [] {
            auto msg = uint8_vector::from_hex("011a2d964a095820a2dbee7247273125ea1c3b9f140d2ea370c705b55e03c36d8d60c6535920f9c6"sv);
            auto sig = uint8_vector::from_hex("f7f5863db6794e3fc6d2b55799c8d398bb76ff95cb045a2c765f2a591ccd3c96ade745f874b806554aca4676b78f309496b28851b9afe5ed6b90e3e4ddcee501"sv);
            auto vkey_full = uint8_vector::from_hex("4b74a06bcb73309388b61daf0687ca66811d4dc2ca657cb7ded31fe5c4698e02413ef6845df77b3461da601348710be44572c7df5b2445a92cf0072005847494"sv);
            auto vkey = buffer(vkey_full.data(), 32);
            expect(crypto::ed25519::verify(sig, vkey, msg));
        };

        "boundary block hash"_test = [] {
            auto genesis_hash = uint8_vector::from_hex("5F20DF933584822601F9E3F8C024EB5EB252FE8CEFB24D1317DC3D432E940EBB");
            auto hash = uint8_vector::from_hex("89D9B5A5B8DDC8D7E5A6795E9774D97FAF1EFEA59B2CAF7EAF9F8C5B32059DF4");
            auto chunk = zstd::read("./data/chunk-registry/compressed/chunk/526D236112DB8E38E66F37D330C85AFE0C268D81DF853DDDE4E88551EB9B0637.zstd");
            auto block_tuple = cbor::zero2::parse(chunk);
            auto &it = block_tuple.get().array();
            const byron::boundary_block blk { it.read().uint(), 0, 2, it.read(), cardano::config::get() };
            expect_equal(0, blk.era());
            expect_equal(0, blk.slot());
            expect_equal(genesis_hash, blk.prev_hash());
            expect_equal(hash, blk.hash());
        };

        "block body hashes verification"_test = [] {
            const auto chunk = zstd::read("./data/chunk-registry/compressed/chunk/526D236112DB8E38E66F37D330C85AFE0C268D81DF853DDDE4E88551EB9B0637.zstd");
            cbor::zero2::decoder dec { chunk };
            while (!dec.done()) {
                auto &block_tuple = dec.read();
                auto &it = block_tuple.array();
                const auto era = it.read().uint();
                auto &block = it.read();
                if (era == 1) [[likely]] {
                    const byron::block blk { era, 0, 2, block, cardano::config::get() };
                    expect(blk.body_hash_ok()) << blk.slot();
                    expect(blk.signature_ok()) << blk.slot();
                } else {
                    const byron::boundary_block blk { era, 0, 2, block, cardano::config::get() };
                }
            }
        };

        "witness key matches the address"_test = [] {
            struct test_vec {
                const char *addr;
                const char *vk;
                uint8_t typ;
            };
            static std::vector<test_vec> test_vecs {
                {
                    "82D818582183581C4041ADF6B03851A9C85DB3F028995504FB4BA48B50703AB1B9841350A0021AD658E71F",
                    "8C0BDEDFBBAB26A1308300512FFB1B220F068EE13F7612AFB076C22DE3FB7641",
                    2
                },
                {
                    "83581C6C9982E7F2B6DCC5EAA880E8014568913C8868D9F0F86EB687B2633CA101581E581C010D876783FB2B4D0D17C86DF29AF8D35356ED3D1827BF4744F0670000",
                    "42A2100A4BCE0F08ED211F980D7A848915FD48953BE80B4B4FB3A9BBF8AEA206CC8A84C83896F3D716FE0FC6AE8D5AE5554109C1FFF5B6CA6C53CC74741DCAD2",
                    0
                },
                {
                    "83581C396B2A0FFEEB442E50E5F07B18AC2CD708063E36A64D878C4404D085A101581E581C23CB6A8782C420E7D0EA8676E3E6960887625ADBE21A270A6C3B8C0600",
                    "BAB0C9781244A2EAA6BF2467C1A3D253AB63C35B4757D4BCE9E0F573D2F81FED3C93A0E359137F5842E0DF4506477F4AEE4DB10B75E83ADBDF40D09CCEB2A911",
                    0
                }
            };
            for (size_t i = 0; i < test_vecs.size(); ++i) {
                const auto &test = test_vecs[i];
                const auto vk = uint8_vector::from_hex(test.vk);
                const auto addr_raw = uint8_vector::from_hex(test.addr);
                const auto addr = byron_addr::from_bytes(addr_raw);
                expect_equal(true, addr.vkey_ok(vk, test.typ), fmt::format("test vector #{}: {}", i, test.addr));
            }
        };

        "raw witness matches the address"_test = [] {
            struct test_vec {
                const char *addr;
                const char *wit;
                uint8_t typ;
            };
            static std::vector<test_vec> test_vecs {
                {
                    "83581CB8C8F782060CD0A476DFDF618DDDE592CC6BEFE21F7AA660440102E7A101581E581CDFA7732F2BBD0E7D8BFEF150118840FAF14D0A0F866FC4174A193A6700",
                    "825840C241211B30321DB26E23A59936C24F564EFC6AC37C83534BDE3C55F4EA96ECC3D9FB3C2A0196F167267882536379EABA489F3A5B0D47C588B0DA4E97A0914B3158400907D5F73F96D012CC0A6BA1F54D733A9BD6BDA6C6FF24025A458EE0D3295EF81F7AB3C16AB769F689FE402893A4531CFD5B333E958D8E5C02C1315F73400406",
                    0
                },
                {
                    "83581C41276B4C0BF10E73952582144E5B016497A988F99E8EFEB661C637F9A101581E581CDFA7732F2BBD0E22A199D85000EF78E8387F043DBD8FCC3C2D92E94000",
                    "825840761C5F8190BD7618646CFA87E7D4995CA72E69EB686D666B81617602685602602118DFF5A46E1F2D08BAFB5379CC59E978F061FB45E9754A16F4BFE1B2C89AEB5840AB65F586B29D7A732BAD39019CAE8A8B1D4D5C1C613C402629EACF2C5FC9D9419FE8A0E8018F3091A9C3E3E6F327A8A219ED41E3249334C79BB94FA280F97B04",
                    0
                }
            };
            for (size_t i = 0; i < test_vecs.size(); ++i) {
                const auto &test = test_vecs[i];
                const auto wit_raw = uint8_vector::from_hex(test.wit);
                auto wit = cbor::zero2::parse(wit_raw);
                auto &it = wit.get().array();
                const auto vk = it.read().bytes();
                const auto addr_raw = uint8_vector::from_hex(test.addr);
                const auto addr = byron_addr::from_bytes(addr_raw);
                expect_equal(true, addr.vkey_ok(vk, test.typ), fmt::format("test vector #{}: {}", i, test.addr));
            }

            "body_hash_ok"_test = [] {
                const auto chunk = zstd::read("./data/chunk-registry/compressed/chunk/526D236112DB8E38E66F37D330C85AFE0C268D81DF853DDDE4E88551EB9B0637.zstd");
                cbor::zero2::decoder dec { chunk };
                while (!dec.done()) {
                    auto &block_tuple = dec.read();
                    auto &it = block_tuple.array();
                    const auto era = it.read().uint();
                    if (era == 1) {
                        const byron::block blk { era, 0, 2, it.read(), cardano::config::get() };
                        expect(blk.body_hash_ok());
                        expect(blk.signature_ok());
                    } else {
                        expect_equal(0, era);
                    }
                }
            };
        };
    };  
};
