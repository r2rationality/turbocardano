/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/common/test.hpp>
#include <turbo/crypto/secp256k1.hpp>
#include <turbo/crypto/sha2.hpp>

using namespace turbo;
using namespace turbo::crypto::secp256k1;

suite crypto_secp256k1_suite = [] {
    "crypto::secp256k1"_test = [] {
        struct test_vec {
            const char *vk;
            const char *msg;
            const char *sig;
        };
        "ecdsa"_test = [] {
            for (const auto &vec: {
                test_vec { "032e433589dce61863199171f4d1e3fa946a5832621fcd29559940a0950f96fb6f", "",
                    "4941155e2303988a1be97a021fbaf9fe6064d05ea694bc5e89328f297154e5c63a2f3e7b5f509294a4c2e22feb697a16b792fabfebe9d0f38403b1c929836b5a" }
            }) {
                expect_equal(true, ecdsa::verify(uint8_vector::from_hex(vec.sig), uint8_vector::from_hex(vec.vk),
                    crypto::sha2::digest(std::string_view { vec.msg })));
            }
            for (const auto &vec: {
                test_vec { "02599de3e582e2a3779208a210dfeae8f330b9af00a47a7fb22e9bb8ef596f301b", "0000000000000000000000000000000000000000000000000000000000000000",
                    "354b868c757ef0b796003f7c23dd754d2d1726629145be2c7b7794a25fec80a09dab0f6ea6ca0cc46e4314e92b900d7d6b493e4b47b6fb999fd9e841575e602d" }
            }) {
                expect_equal(false, ecdsa::verify(uint8_vector::from_hex(vec.sig), uint8_vector::from_hex(vec.vk), uint8_vector::from_hex(vec.msg)));
            }
        };
        "schnorr"_test = [] {
            for (const auto &vec: {
                test_vec { "f9308a019258c31049344f85f89d5229b531c845836f99b08601f113bce036f9", "0000000000000000000000000000000000000000000000000000000000000000",
                    "e907831f80848d1069a5371b402410364bdf1c5f8307b0084c55f1ce2dca821525f66a4a85ea8b71e482a74f382d2ce5ebeee8fdb2172f477df4900d310536c0" }
            }) {
                expect_equal(true, schnorr::verify(uint8_vector::from_hex(vec.sig), uint8_vector::from_hex(vec.vk), uint8_vector::from_hex(vec.msg)));
            }
            for (const auto &vec: {
                test_vec { "599de3e582e2a3779208a210dfeae8f330b9af00a47a7fb22e9bb8ef596f301b", "0000000000000000000000000000000000000000000000000000000000000001",
                    "5a56da88e6fd8419181dec4d3dd6997bab953d2fc71ab65e23cfc9e7e3d1a310613454a60f6703819a39fdac2a410a094442afd1fc083354443e8d8bb4461a9b" }
            }) {
                expect_equal(false, schnorr::verify(uint8_vector::from_hex(vec.sig), uint8_vector::from_hex(vec.vk), uint8_vector::from_hex(vec.msg)));
            }
        };
    };
};