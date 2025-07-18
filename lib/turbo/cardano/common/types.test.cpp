/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cardano/common/config.hpp>
#include <turbo/cardano/common/types.hpp>
#include <turbo/common/test.hpp>

namespace {
    using namespace turbo;
    using namespace turbo::cardano;
}

suite cardano_type_test = [] {
    "cardano::type"_test = [] {
        "address_buf"_test = [] {
            address_buf addr1 { "0xDEADBEAF" };
            expect(addr1.size() == 4_u);
            expect(addr1[0] == 0xDE);
            expect(addr1[1] == 0xAD);
            expect(addr1[2] == 0xBE);
            expect(addr1[3] == 0xAF);

            uint8_t exp[] = {
                0xf1, 0xc3, 0x7b, 0x1b, 0x5d, 0xc0, 0x66, 0x9f, 0x1d, 0x3c, 0x61, 0xa6, 0xfd, 0xdb, 0x2e, 0x8f,
                0xde, 0x96, 0xbe, 0x87, 0xb8, 0x81, 0xc6, 0x0b, 0xce, 0x8e, 0x8d, 0x54, 0x2f
            };
            address_buf addr2 { "stake178phkx6acpnf78fuvxn0mkew3l0fd058hzquvz7w36x4gtcccycj5" };
            expect(addr2.size() == sizeof(exp));
            expect(memcmp(addr2.data(), exp, sizeof(exp)) == 0_i);
        };
        "address pointer"_test = [] {
            auto buf = uint8_vector::from_hex("4186880a8bb19ec8742db9076795c5107f7ffc65a889e7b0980ffeaca20c0c0c");
            cardano::address addr { buf };
            expect(addr.has_pay_id());
            expect(addr.pay_id() == cardano::pay_ident { cardano::key_hash::from_hex("86880a8bb19ec8742db9076795c5107f7ffc65a889e7b0980ffeaca2") });
            expect(addr.has_pointer());
            auto ptr = addr.pointer();
            expect(ptr.slot == 12_u);
            expect(ptr.tx_idx == 12_u);
            expect(ptr.cert_idx == 12_u);
        };
        "address pointer 2"_test = [] {
            auto buf = uint8_vector::from_hex("41fbfce15acccb420982704c9e591f83ab3315c3314a18ecf65346e0858292b3380b00");
            cardano::address addr { buf };
            expect(addr.has_pay_id());
            expect(addr.pay_id() == cardano::pay_ident { cardano::key_hash::from_hex("fbfce15acccb420982704c9e591f83ab3315c3314a18ecf65346e085") });
            expect(addr.has_pointer());
            auto ptr = addr.pointer();
            expect(ptr.slot == 4495800_u);
            expect(ptr.tx_idx == 11_u);
            expect(ptr.cert_idx == 0_u);
        };
        "address pointer extra data"_test = [] {
            const auto orig = uint8_vector::from_hex("411134F9ACC87389270CBE27BB0988D6B532CD705C9919719804D3E800FFFFFFFE808080808089CAC3640200");
            const auto exp = uint8_vector::from_hex("411134F9ACC87389270CBE27BB0988D6B532CD705C9919719804D3E80089CAC3640200");
            const address addr { orig };
            expect_equal(addr.bytes(), exp);
        };
        "param_update"_test = [] {
            param_update u1 {};
            u1.rehash();
            param_update u2 {};
            u2.rehash();
            expect(u1.hash == u2.hash);
            expect(u1 == u2);
            u1.decentralization.emplace(2, 10);
            u1.rehash();
            expect(u1.hash != u2.hash);
            expect(u1 != u2);
            u2.decentralization.emplace(2, 10);
            u2.rehash();
            expect(u1.hash == u2.hash);
            expect(u1 == u2);
        };
        "slot"_test = [] {
            const configs_dir cfgs { "etc/sanchonet" };
            const cardano::config c_cfg { cfgs };
            const slot s1 { 86300, c_cfg };
            const slot s2 { 86400, c_cfg };
            expect(s1.epoch() != s2.epoch());
            expect(s1.chunk_id() != s2.chunk_id());
        };
        "byron redeem address #1"_test = [] {
            const std::string_view redeem_vk { "AAG3vJwTzCcL0zp2-1yfI-mn_7haYvSYJln2xR_aBS8=" };
            const auto my_addr_protected = byron_avvm_addr(redeem_vk);
            const auto addr_protected = from_haskell("\\130\\216\\CANX!\\131X\\FS\\131\\132\\243\\236FaTKh\\199w\\132\\246\\148\\206*\\212\\226\\tpW\\184\\162\\224\\186;6\\185\\160\\STX\\SUBp\\f7m");
            expect_equal(addr_protected, my_addr_protected);
        };
        "byron redeem address"_test = [] {
            struct test_vec {
                std::string vk {};
                std::string tx_id {};
            };
            std::vector<test_vec> test_vectors {
                { "-0BJDi-gauylk4LptQTgjMeo7kY9lTCbZv12vwOSTZk=", "8EE33C9906974706223D7D500D63BBEE2369D7150F972757A9FDDED2F706B938" },
                { "DKsNZvWNRoVbR4wIVAM2XE3IQ8vyPeE1Q8oRtraauiM=", "633BEEF5CE862F414E61C4963F57B266B7A5C46F91274141DBA6CC1063C74204" }
            };
            for (const auto &[vk, tx_id]: test_vectors) {
                expect_equal(tx_hash::from_hex(tx_id), byron_avvm_tx_hash(vk));
            }
        };
    };
};