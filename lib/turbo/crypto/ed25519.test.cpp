/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/common/bytes.hpp>
#include <turbo/common/test.hpp>
#include <turbo/crypto/blake2b.hpp>
#include <turbo/crypto/ed25519.hpp>

namespace {
    using namespace turbo;
    using namespace turbo::crypto;
}

suite crypto_ed25519_suite = [] {
    "crypto::ed25519"_test = [] {
        "verify-test-vector"_test = [] {
            static const ed25519::vkey vk {
                0xe5, 0x2e, 0x09, 0xb2, 0xd3, 0x76, 0x3c, 0x57, 0x00, 0xd5, 0x41, 0xed, 0x9b, 0x88, 0xbe, 0xbd,
                0xf8, 0x5b, 0x4a, 0x41, 0xd5, 0x42, 0x1a, 0xf1, 0x88, 0x85, 0x46, 0x98, 0x10, 0xf3, 0x17, 0xf7 };
            static const ed25519::signature sig {
                0xa0, 0xdb, 0x79, 0x88, 0x7b, 0xcb, 0xb9, 0x2e, 0xe9, 0xcf, 0xe9, 0x0e, 0x83, 0x2c, 0x75, 0xab,
                0xdb, 0xcb, 0xe7, 0x10, 0xb6, 0x29, 0x76, 0x55, 0x35, 0x59, 0x11, 0x33, 0xb4, 0xf2, 0xb6, 0xe6,
                0xad, 0xfa, 0xb9, 0x33, 0xa8, 0x96, 0xda, 0x75, 0xf2, 0xcd, 0x5d, 0xb3, 0xa3, 0x35, 0x4a, 0x27,
                0x3d, 0x3e, 0x37, 0xc7, 0x28, 0xca, 0x98, 0x07, 0x53, 0x8d, 0x83, 0x8f, 0xef, 0xbb, 0x2f, 0x00 };
            static const byte_array<84> msg {
                0xa3, 0x00, 0x81, 0x82, 0x58, 0x20, 0xc1, 0xa1, 0xff, 0x8e, 0x54, 0x99, 0xc3, 0x9f, 0xfa, 0x4c,
                0x70, 0x67, 0x43, 0x78, 0x5e, 0x62, 0x17, 0xa3, 0x3d, 0xf4, 0x8c, 0xef, 0x73, 0x42, 0xd0, 0xc4,
                0x52, 0x60, 0x51, 0x58, 0x50, 0xa1, 0x00, 0x01, 0x81, 0x82, 0x58, 0x1d, 0x61, 0xc2, 0x6a, 0xc0,
                0x99, 0x31, 0xf2, 0xff, 0x67, 0x58, 0x57, 0x30, 0x9b, 0xe6, 0xea, 0xf2, 0xd4, 0xbc, 0x18, 0xd2,
                0xdd, 0x33, 0xf5, 0x29, 0x0f, 0xc3, 0xa2, 0xad, 0xd1, 0x1a, 0x01, 0x54, 0x45, 0x60, 0x02, 0x1a,
                0x00, 0x0a, 0xae, 0x60 };
            auto hash = blake2b::digest(msg);
            expect(ed25519::verify(sig, vk, msg) == false);
            expect(ed25519::verify(sig, vk, hash) == true);
        };
        "create-sign-verify"_test = [] {
            ed25519::skey sk1 {}, sk2 {};
            ed25519::vkey vk1 {}, vk2 {};
            ed25519::create(sk1, vk1);
            ed25519::create(sk2, vk2);
            expect(sk1 != sk2);
            expect(vk1 != vk2);
            std::string msg1 { "message1" }, msg2 { "message2" };
            ed25519::signature sig11 {}, sig12 {}, sig21 {}, sig22 {};
            ed25519::sign(sig11, msg1, sk1);
            ed25519::sign(sig12, msg2, sk1);
            ed25519::sign(sig21, msg1, sk2);
            ed25519::sign(sig22, msg2, sk2);
            expect(sig11 != sig12);
            expect(sig11 != sig21);
            expect(sig21 != sig22);
            expect(ed25519::verify(sig11, vk1, msg1));
            expect(!ed25519::verify(sig11, vk2, msg1));
            expect(!ed25519::verify(sig11, vk1, msg2));
            expect(ed25519::verify(sig12, vk1, msg2));
            expect(!ed25519::verify(sig12, vk2, msg2));
            expect(!ed25519::verify(sig12, vk1, msg1));
            expect(ed25519::verify(sig21, vk2, msg1));
            expect(!ed25519::verify(sig21, vk1, msg1));
            expect(!ed25519::verify(sig21, vk2, msg2));
            expect(ed25519::verify(sig22, vk2, msg2));
            expect(!ed25519::verify(sig22, vk1, msg2));
            expect(!ed25519::verify(sig22, vk2, msg1));
        };
        "extract-vkey"_test = [] {
            ed25519::skey sk {};
            ed25519::vkey vk1 {};
            ed25519::create(sk, vk1);
            ed25519::vkey vk2 {};
            ed25519::extract_vk(vk2, sk);
            expect(vk2 == vk1);
        };
        "create-seed"_test = [] {
            auto seed1 = crypto::blake2b::digest<ed25519::seed>(std::string_view { "1" });
            auto seed2 = crypto::blake2b::digest<ed25519::seed>(std::string_view { "2" });
            expect(seed1 != seed2);
            ed25519::skey sk1 {}, sk2 {};
            ed25519::vkey vk1 {}, vk2 {};
            ed25519::create_from_seed(sk1, vk1, seed1);
            ed25519::create_from_seed(sk2, vk2, seed2);
            expect(sk1 != sk2);
            expect(vk1 != vk2);
            std::string msg1 { "message1" }, msg2 { "message2" };
            ed25519::signature sig11 {}, sig12 {}, sig21 {}, sig22 {};
            ed25519::sign(sig11, msg1, sk1);
            ed25519::sign(sig12, msg2, sk1);
            ed25519::sign(sig21, msg1, sk2);
            ed25519::sign(sig22, msg2, sk2);
            expect(sig11 != sig12);
            expect(sig11 != sig21);
            expect(sig21 != sig22);
            expect(ed25519::verify(sig11, vk1, msg1));
            expect(!ed25519::verify(sig11, vk2, msg1));
            expect(!ed25519::verify(sig11, vk1, msg2));
            expect(ed25519::verify(sig12, vk1, msg2));
            expect(!ed25519::verify(sig12, vk2, msg2));
            expect(!ed25519::verify(sig12, vk1, msg1));
            expect(ed25519::verify(sig21, vk2, msg1));
            expect(!ed25519::verify(sig21, vk1, msg1));
            expect(!ed25519::verify(sig21, vk2, msg2));
            expect(ed25519::verify(sig22, vk2, msg2));
            expect(!ed25519::verify(sig22, vk1, msg2));
            expect(!ed25519::verify(sig22, vk2, msg1));
        };
    };
};