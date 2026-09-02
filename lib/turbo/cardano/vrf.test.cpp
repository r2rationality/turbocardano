/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include "vrf.hpp"
#include <turbo/common/bytes.hpp>
#include <turbo/common/file.hpp>
#include <turbo/common/test.hpp>
#include <turbo/crypto/blake2b.hpp>
#include <turbo/math/rational.hpp>

namespace {
    using namespace std::literals::string_view_literals;
    using namespace turbo;
    using namespace turbo::cardano;
}

suite cardano_vrf_suite = [] {
    "cardano::vrf"_test = [] {
        // vector 30 from C VRF library
        "verify_static_C_30"_test = [] {
            vrf_vkey vkey {0x5b,0x14,0xfd,0xe6,0xdb,0x37,0xa5,0x30,0x2f,0x01,0x50,0xa7,0x3f,0x6a,0xda,0x5a,0x05,0xa8,0x10,0xa2,0x4c,0x65,0x64,0x82,0xcb,0xde,0xb5,0x10,0x20,0xe9,0x2f,0xe7};
            vrf_proof proof {0x31,0xcd,0xab,0x62,0x0b,0xdf,0x82,0x31,0xab,0xce,0xe3,0x2d,0x61,0x5a,0xf3,0x46,0x49,0xe6,0x9b,0x13,0x22,0x86,0xcc,0x59,0xf9,0x50,0xf2,0x35,0x52,0x84,0xd9,0x53,0xa6,0xb7,0xea,0x97,0x5a,0x2f,0xd8,0x9a,0xf4,0x43,0xa1,0x5c,0x07,0x63,0x99,0xf3,0x90,0xc9,0x4a,0x6a,0xa3,0xf5,0xdb,0xf8,0xe1,0x57,0x67,0x80,0xa7,0xe1,0x6f,0xa4,0x8c,0x3a,0x09,0x59,0x72,0xac,0x3a,0xb1,0xe1,0xaa,0x07,0x6d,0x92,0x46,0xac,0x09};
            vrf_result res {0x15,0x2d,0xca,0x76,0x91,0x2d,0x4a,0x38,0x60,0x6f,0x46,0xd9,0x0d,0x4b,0x87,0x8b,0x3e,0x60,0xb9,0xce,0xf8,0x74,0x0e,0xf3,0x22,0x29,0x0f,0x18,0xa6,0x7a,0xd4,0x8e,0x71,0x6f,0x41,0x2e,0x70,0x13,0xd5,0xe1,0xee,0x4c,0x4a,0xaf,0x5e,0x7f,0x86,0xd1,0x58,0x24,0x9b,0xcd,0x06,0x7f,0x5e,0x62,0xb6,0x2c,0x25,0xb7,0x16,0x6b,0x94,0x29};
            byte_array<30> msg { 0x23, 0x0d, 0xd4, 0xc8, 0x55, 0xc1, 0x33, 0xc5, 0xb3, 0xc2, 0x4a, 0x72, 0xaf, 0x9b, 0xbb, 0xc4, 0x82, 0x05, 0x98, 0x4e, 0xa4, 0xf2, 0x04, 0x5f, 0xea, 0xac, 0x17, 0xfe, 0x1a, 0xf9 };
            expect(vrf03_verify(res, vkey, proof, msg)) << "VRF verification failed";
        };

        // vector 29 from Rust VRF library
        "verify_static_rust_1"_test = [] {
            auto vkey = uint8_vector::from_hex("5ca0ed2b774adf3bae5e3e7da2f8ec877d9f063cc3d7050a6c49dfbbe2641dec");
            auto proof = uint8_vector::from_hex("02180c447320b66012420971b70b448d11fead6d6e334c398f4daf01ccd92bfbcc4a8730a296ab33241f72da3c3a1fd53f1206a2b9f27ff6a5d9b8860fd955c39f55f9293ab58d1a2c18d555d2686101");
            auto result = uint8_vector::from_hex("deb23fdc1267fa447fb087796544ce02b0580df8f1927450bed0df134ddc3548075ed48ffd72ae2a9ea65f79429cfbe2e15b625cb239ad0ec3910003765a8eb3");
            auto msg = uint8_vector::from_hex("fc9f719740f900ee2809f6fdcf31bb6f096f0af133c604a27aaf85379c");
            expect(vrf03_verify(result, vkey, proof, msg)) << "VRF verification failed";
        };

        // Test vectors from https://datatracker.ietf.org/doc/html/draft-irtf-cfrg-vrf-03#page-40

        "verify_static_ietf_1"_test = [] {
            auto vkey = uint8_vector::from_hex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
            auto proof = uint8_vector::from_hex("b6b4699f87d56126c9117a7da55bd0085246f4c56dbc95d20172612e9d38e8d7ca65e573a126ed88d4e30a46f80a666854d675cf3ba81de0de043c3774f061560f55edc256a787afe701677c0f602900");
            auto result = uint8_vector::from_hex("5b49b554d05c0cd5a5325376b3387de59d924fd1e13ded44648ab33c21349a603f25b84ec5ed887995b33da5e3bfcb87cd2f64521c4c62cf825cffabbe5d31cc");
            auto msg = uint8_vector::from_hex("");
            expect(vrf03_verify(result, vkey, proof, msg)) << "VRF verification failed";
        };

        "verify_static_ietf_2"_test = [] {
            auto vkey = uint8_vector::from_hex("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c");
            auto proof = uint8_vector::from_hex("ae5b66bdf04b4c010bfe32b2fc126ead2107b697634f6f7337b9bff8785ee111200095ece87dde4dbe87343f6df3b107d91798c8a7eb1245d3bb9c5aafb093358c13e6ae1111a55717e895fd15f99f07");
            auto result = uint8_vector::from_hex("94f4487e1b2fec954309ef1289ecb2e15043a2461ecc7b2ae7d4470607ef82eb1cfa97d84991fe4a7bfdfd715606bc27e2967a6c557cfb5875879b671740b7d8");
            auto msg = uint8_vector::from_hex("72");
            expect(vrf03_verify(result, vkey, proof, msg)) << "VRF verification failed";
        };

        "verify_static_ietf_3"_test = [] {
            auto vkey = uint8_vector::from_hex("fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025");
            auto proof = uint8_vector::from_hex("dfa2cba34b611cc8c833a6ea83b8eb1bb5e2ef2dd1b0c481bc42ff36ae7847f6ab52b976cfd5def172fa412defde270c8b8bdfbaae1c7ece17d9833b1bcf31064fff78ef493f820055b561ece45e1009");
            auto result = uint8_vector::from_hex("2031837f582cd17a9af9e0c7ef5a6540e3453ed894b62c293686ca3c1e319dde9d0aa489a4b59a9594fc2328bc3deff3c8a0929a369a72b1180a596e016b5ded");
            auto msg = uint8_vector::from_hex("af82");
            expect(vrf03_verify(result, vkey, proof, msg)) << "VRF verification failed";
        };

        "verify-praos-leader-vrf"_test = [&] {
            auto vkey = file::read("./data/vrf-vkey.bin"sv);
            auto proof = file::read("./data/vrf-leader-proof.bin"sv);
            auto result = file::read("./data/vrf-leader-result.bin"sv);
        
            uint64_t slot = 4492800;
            auto uc_nonce = uint8_vector::from_hex("12dd0a6a7d0e222a97926da03adb5a7768d31cc7c5c2bd6828e14a7d25fa3a60");
            auto epoch_nonce = uint8_vector::from_hex("1a3be38bcbb7911969283716ad7aa550250226b76a61fc51cc9a9a35d9276d81");
            auto vrf_input = vrf_make_seed(uc_nonce, slot, epoch_nonce);
            expect(vrf03_verify(result, vkey, proof, vrf_input)) << "leader VRF verification failed with input:" << vrf_input;
        };

        "verify-praos-nonce-vrf"_test = [&] {
            auto vkey = file::read("./data/vrf-vkey.bin"sv);
            auto proof = file::read("./data/vrf-nonce-proof.bin"sv);
            auto result = file::read("./data/vrf-nonce-result.bin"sv);
            uint64_t slot = 4492800;
            auto uc_nonce = uint8_vector::from_hex("81e47a19e6b29b0a65b9591762ce5143ed30d0261e5d24a3201752506b20f15c");
            auto epoch_nonce = uint8_vector::from_hex("1a3be38bcbb7911969283716ad7aa550250226b76a61fc51cc9a9a35d9276d81");
            auto vrf_input = vrf_make_seed(uc_nonce, slot, epoch_nonce);
            expect(vrf03_verify(result, vkey, proof, vrf_input)) << "nonce VRF verification failed with input:" << vrf_input;
        };

        "vrf-nonce-from-vrf-result"_test = [] {
            auto vrf_out = file::read("./data/vrf-nonce-result.bin"sv);
            expect(vrf_out.size() == 64) << vrf_out.size();
            auto eta_exp = uint8_vector::from_hex("44ce562e2e41da07693b78411c39f68999a1ba0c46f5144f1fab42889edf6311");
            auto eta = crypto::blake2b::digest(vrf_out);
            expect(eta_exp == eta) << "Failed to construct nonce from VRF output: expected:" << eta_exp << "got:" << eta;
        };

        "vrf-leader-result-epoch-209"_test = [] {
            auto vkey = vrf_vkey::from_hex("6D930CC9D1BAADE5CD1C70FBC025D3377CE946760C48E511D1ABDF8ACFF6FF1C");
            auto result = vrf_result::from_hex("95FAE02F6724F6401EDAF4F2E847AE1E6792D1842FBAD2B828DD2D54811F49DC014B3DD435059E667C40F86625809338B2AE048FA87C0C85DE6C1F40C70EEC32");
            auto proof = vrf_proof::from_hex("2456E5E98914B9A8F8D0367AC4C06EDE978CA7BEF8B602D79B3309DCD0F9E7B3FF1DE476F10AC393861A93330190F69C002E9F40F9D9AA2F0215DCED3423789C3FFDD95C8B25EE7FCD36229DCDB3530A");
            auto uc_nonce = vrf_nonce::from_hex("12dd0a6a7d0e222a97926da03adb5a7768d31cc7c5c2bd6828e14a7d25fa3a60");
            uint64_t slot = 4924800;
            auto epoch_nonce = vrf_nonce::from_hex("ea98cb2dac7208296ac89030f24cdc0dc6fbfebc4bf1f5b7a8331ec47e3bb311");
            auto vrf_input = vrf_make_seed(uc_nonce, slot, epoch_nonce);
            expect(vrf03_verify(result, vkey, proof, vrf_input)) << "leader VRF verification failed with input:" << vrf_input;
        };

        "vrf-nonce-accumulate"_test = [] {
            auto eta_prev = uint8_vector::from_hex("1a3be38bcbb7911969283716ad7aa550250226b76a61fc51cc9a9a35d9276d81");
            auto eta_new = uint8_vector::from_hex("44ce562e2e41da07693b78411c39f68999a1ba0c46f5144f1fab42889edf6311");
            auto eta_next_exp = uint8_vector::from_hex("2af15f57076a8ff225746624882a77c8d2736fe41d3db70154a22b50af851246");
            auto eta_next = vrf_nonce_accumulate(eta_prev, eta_new);
            expect(eta_next == eta_next_exp) << "VRF accumulation failed: expected:" << eta_next_exp << "got:" << eta_next;
        };

        "vrf-nonce-epoch-transition-210"_test = [] {
            // ηc ⭒ ηh ⭒ extraEntropy
            auto eta_c = uint8_vector::from_hex("a9543bc3820138abfaaad606d19c50df70c896336a88ab01da0eb34c1129bf31");
            auto eta_h = uint8_vector::from_hex("dfc1d6e6dbce685b5cf85899c6e3c89539b081c62222265910423ced4096390a");
            auto eta_next_exp = uint8_vector::from_hex("ddf346732e6a47323b32e1e3eeb7a45fad678b7f533ef1f2c425e13c704ba7e3");
            auto eta_next = vrf_nonce_accumulate(eta_c, eta_h);
            expect(eta_next == eta_next_exp) << "VRF epoch transition failed: expected:" << eta_next_exp << "got:" << eta_next;
        };

        "vrf-nonce-epoch-transition-259"_test = [] {
            auto eta_c = uint8_vector::from_hex("d1340a9c1491f0face38d41fd5c82953d0eb48320d65e952414a0c5ebaf87587");
            auto eta_h = uint8_vector::from_hex("ee91d679b0a6ce3015b894c575c799e971efac35c7a8cbdc2b3f579005e69abd");
            auto entropy = uint8_vector::from_hex("d982e06fd33e7440b43cefad529b7ecafbaa255e38178ad4189a37e4ce9bf1fa");
            auto eta_next_exp = uint8_vector::from_hex("0022cfa563a5328c4fb5c8017121329e964c26ade5d167b1bd9b2ec967772b60");
            auto eta_next = vrf_nonce_accumulate(vrf_nonce_accumulate(eta_c, eta_h), entropy);
            expect(eta_next == eta_next_exp) << "VRF epoch transition failed: expected:" << eta_next_exp << "got:" << eta_next;
        };

        "era-6-make-input"_test = [&] {
            auto epoch_nonce = uint8_vector::from_hex("cffa84169d13ef1106db5a72b491d27b2193317698762e5b3f362d0c50963426");
            auto slot = 74260800;
            auto input_exp = uint8_vector::from_hex("8ac7a6f38d371aa567176db414ae07d8540b920d29f260d0770553c8c1fd79dc");
            auto vrf_input = vrf_make_input(slot, epoch_nonce);
            expect(input_exp == vrf_input) << vrf_input << "!=" << input_exp;
        };

        "era-6-vrf_nonce-value-76314088"_test = [&] {
            auto vrf_result = uint8_vector::from_hex("96891e4739ecab42e5d54a1b2593ab84b57a0bcdd6a3cabaa2fb84d3c205cf365398130d9bba22408cfe9b18e5fbbff672bb8fd073d703894fe3bea4739742c1");
            auto nonce_exp = uint8_vector::from_hex("f0a84689ddeec829b99ecf1462423df8db218ce46a60987135d54634163a9182");
            expect(nonce_exp == vrf_nonce_value(vrf_result));
        };

        "era-6-vrf_nonce-value-77165950"_test = [&] {
            auto vrf_result = uint8_vector::from_hex("a5f7a9efbcd6e2b4c6159c352acb1ae042043b9d04af4f72cee126dffde9062593b4a1d56942b95b220ba439e5ca8fea86106771996155cfec0a4b18e1322238");
            auto nonce_exp = uint8_vector::from_hex("9fda26c536a6dc10094563625cb04c4f92e01731e3df0ba94721e2ddba5c5632");
            expect(nonce_exp == vrf_nonce_value(vrf_result));
        };

        "era 6 vrf_leader_value"_test = [&] {
            auto result = vrf_result::from_hex("288899B5EB24C0D3F7A81EB60549B6EA8461320B6FBF369831D11864EFD3DFD7A6198A7A2C9DE8F85307FA83A8F6ECC51A3DFFBB6510480D96D0C149781C0463");
            auto exp_value = vrf_nonce::from_hex("0003b2d342e4f2fe108b32434d5d92f4b729f28257dcbff5a67ccbeda24cdd4e");
            auto act_value = vrf_leader_value(result);
            expect(act_value == exp_value) << fmt::format("{}", act_value);
        };

        "era 6 vrf_leader_value #2"_test = [&] {
            auto result = vrf_result::from_hex("8acb3e9bdf8b0826cd5ab0d25063618169d1fcbf82e654f7133edef22270d9b8674061a64d8a3ae26ed3d5d94b61ac89ea48d3d378ab1f21c3e9950bbb1fb6b2");
            auto exp_value = vrf_nonce::from_hex("00003eddb685ab0c3bf94e97df66c820ef1c3dd11c628a3199933ee701657202");
            auto act_value = vrf_leader_value(result);
            expect(act_value == exp_value) << fmt::format("{}", act_value);
        };

        /*"era 6 vrf_leader_value_nat #2"_test = [&] {
            auto leader_val_bin = vrf_nonce::from_hex("00003eddb685ab0c3bf94e97df66c820ef1c3dd11c628a3199933ee701657202");
            boost::multiprecision::cpp_int exp_value { "433885643539428655897425832779001864058115449749788967874334011813097986" };
            auto act_value = vrf_leader_value_nat(leader_val_bin);
            expect(act_value == exp_value) << act_value;
        };*/

        "vrf leader-eligibility"_test = [&] {
            auto result = file::read("./data/vrf2-leader-result.bin"sv);
            expect(result.size() == 64_u);
            rational_u64 leader_stake_rel { 124'225'808'029'661, 17'260'167'504'454'384 };
            expect(vrf_leader_is_eligible(result, { 1, 20 }, leader_stake_rel));
            vrf_result maximum {};
            maximum.fill(0xFF);
            expect(!vrf_leader_is_eligible(maximum, { 1, 20 }, leader_stake_rel));
            expect(vrf_leader_is_eligible(maximum, { 1, 1 }, { 0, 1 }));
        };

        "vrf leader-eligibility-epoch"_test = [&] {
            const auto result = vrf_result::from_hex("288899B5EB24C0D3F7A81EB60549B6EA8461320B6FBF369831D11864EFD3DFD7A6198A7A2C9DE8F85307FA83A8F6ECC51A3DFFBB6510480D96D0C149781C0463");
            expect_equal(64, result.size());
            const rational_u64 leader_stake_rel { 32451895600839, 12521840766545450 };
            expect(vrf_leader_is_eligible(vrf_leader_value(result), { 1, 20 }, leader_stake_rel));
        };

        "vrf leader-ineligibility-boundary"_test = [&] {
            const auto result = vrf_result::from_hex("AA90E3DD7253DEF79FB29AA7830709065A1AFEF183DD5EC55B2EC58DA04FC6DFE8EFB3473F5CCDFBBCA23D59ED149BC207A4BF173D3A73D42D49058461A57462");
            const rational_u64 leader_stake_rel { 5441434220155, 3622928300609476 };
            expect(!vrf_leader_is_eligible(vrf_leader_value(result), { 1, 20 }, leader_stake_rel));
        };

        "vrf keypair create, prove, verify"_test = [&] {
            vrf_skey sk {};
            vrf_vkey vk {};
            vrf_seed seed {};
            vrf03_create_from_seed(sk, vk, seed);
            vrf_result res {};
            vrf_proof proof {};
            const std::string msg { "Hello, world!" };
            vrf03_prove(proof, res, sk, msg);
            expect(vrf03_verify(res, vk, proof, msg));
        };
    };
};
