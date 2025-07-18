/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cbor/encoder.hpp>
#include <turbo/cbor/zero2.hpp>
#include <turbo/common/test.hpp>

using namespace turbo;

suite cbor_encoder_suite = [] {
    "cbor::encoder"_test = [] {
        "case_1"_test = [] {
            cbor::encoder enc {};
            enc.bytes(std::string_view { "Hello" }).array(2)
                    .uint(0)
                    .map(1)
                    .uint(7)
                    .array(2)
                    .uint(764824073)
                    .s_false();
            const auto act = enc.cbor();
            const auto exp = uint8_vector::from_hex("4548656c6c6f8200A107821A2D964A09F4");
            expect(act == exp) << act;
        };
        "uint"_test = [] {
            {
                cbor::encoder enc {};
                enc.uint(0xFF);
                const auto act = enc.cbor();
                const auto exp = uint8_vector::from_hex("18FF");
                expect(act == exp) << act;
            }
            {
                cbor::encoder enc {};
                enc.uint(0xFFFF);
                const auto act = enc.cbor();
                const auto exp = uint8_vector::from_hex("19FFFF");
                expect(act == exp) << act;
            }
            {
                cbor::encoder enc {};
                enc.uint(0xFFFFFFFF);
                const auto act = enc.cbor();
                const auto exp = uint8_vector::from_hex("1AFFFFFFFF");
                expect(act == exp) << act;
            }
            {
                cbor::encoder enc {};
                enc.uint(0xFFFFFFFFFF);
                const auto act = enc.cbor();
                const auto exp = uint8_vector::from_hex("1B000000FFFFFFFFFF");
                expect(act == exp) << act;
            }
        };
        "indefinite array"_test = [] {
            cbor::encoder enc {};
            enc.array();
            enc.uint(0);
            enc.uint(1);
            enc.s_break();
            const auto act = enc.cbor();
            const auto exp = uint8_vector::from_hex("9f0001ff");
            expect(act == exp) << act;
        };
        "float32"_test = [] {
            {
                cbor::encoder enc {};
                enc.float32(123.45e-20F);
                expect_equal(enc.cbor(), uint8_vector::from_hex("FA21B62E17"));
                expect_equal(cbor::zero2::parse(enc.cbor()).get().float32(), 123.45e-20F);
            }
            {
                cbor::encoder enc {};
                enc.float32(-9876.33e30F);
                expect_equal(enc.cbor(), uint8_vector::from_hex("FAF7F37868"));
                expect_equal(cbor::zero2::parse(enc.cbor()).get().float32(), -9876.33e30F);
            }
        };
        /*"bigint"_test = [] {
            {
                cbor::encoder enc {};
                cpp_int v { 0x01020304 };
                v <<= 96;
                enc.bigint(v);
                test_same(enc.cbor(), uint8_vector::from_hex("C25001020304000000000000000000000000"));
                const auto parsed = cbor::zero2::parse(enc.cbor());
                test_same(parsed.bigint(), v);
            }
            {
                cbor::encoder enc {};
                enc.bigint(10);
                test_same(enc.cbor(), uint8_vector::from_hex("0A"));
                const auto parsed = cbor::zero2::parse(enc.cbor());
                test_same(parsed.bigint(), cpp_int { 10 });
            }
            {
                cbor::encoder enc {};
                cpp_int v { 0x01020304 };
                v <<= 96;
                v *= -1;
                enc.bigint(v);
                test_same(enc.cbor(), uint8_vector::from_hex("C35001020303FFFFFFFFFFFFFFFFFFFFFFFF"));
                const auto parsed = cbor::zero2::parse(enc.cbor());
                test_same(parsed.bigint(), v);
            }
            {
                cbor::encoder enc {};
                enc.bigint(-10);
                test_same(enc.cbor(), uint8_vector::from_hex("29"));
                const auto parsed = cbor::zero2::parse(enc.cbor());
                test_same(parsed.bigint(), cpp_int { -10 });
            }
            {
                // the largest value encodable as uint
                cbor::encoder enc {};
                const cpp_int exp { 0xFFFFFFFFFFFFFFFFULL };
                enc.bigint(exp);
                test_same(enc.cbor(), uint8_vector::from_hex("1BFFFFFFFFFFFFFFFF"));
                const auto parsed = cbor::zero2::parse(enc.cbor());
                test_same(parsed.bigint(), exp);
            }
            {
                // the largest value encodable as nint
                cbor::encoder enc {};
                cpp_int exp { 0xFFFFFFFFFFFFFFFFULL };
                ++exp;
                exp *= -1;
                enc.bigint(exp);
                test_same(enc.cbor(), uint8_vector::from_hex("3BFFFFFFFFFFFFFFFF"));
                const auto parsed = cbor::zero2::parse(enc.cbor());
                test_same(parsed.bigint(), cpp_int { exp });
            }
        };*/
    };
};