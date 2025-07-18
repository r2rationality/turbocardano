/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <array>
#include <turbo/bech32.hpp>
#include <turbo/common/bytes.hpp>
#include <turbo/common/test.hpp>
#include <turbo/crypto/blake2b.hpp>

using namespace turbo;

static bech32 own_match(const std::string_view &text, const buffer &exp)
{
    bech32 addr(text, false);
    expect(addr.size() == exp.size()) << addr.size() << " != " << exp.size();
    expect(memcmp(addr.data(), exp.data(), exp.size()) == 0_i);
    return addr;
}

suite bech32_suite = [] {
    "bech32"_test = [] {
        auto payment_vk = bech32("addr_vk1w0l2sr2zgfm26ztc6nl9xy8ghsk5sh6ldwemlpmp9xylzy4dtf7st80zhd"sv);
        std::array<uint8_t, 28> payment_hash;
        crypto::blake2b::digest(payment_hash, payment_vk);
        auto stake_vk = bech32("stake_vk1px4j0r2fk7ux5p23shz8f3y5y2qam7s954rgf3lg5merqcj6aetsft99wu"sv);
        std::array<uint8_t, 28> stake_hash;
        crypto::blake2b::digest(stake_hash, stake_vk);
        auto script_hash = bech32("script1cda3khwqv60360rp5m7akt50m6ttapacs8rqhn5w342z7r35m37"sv);

        "extract stake hash"_test = [] {
            auto text = "addr1qxxt8gz54yht9r034s5s42u3ts2xg4aem8exlxhslmfv20vau7usgm94re2n6fhe9ee88c2u5ta5znnwwtlxpsulzrdqke4t3r"sv;
            uint8_t exp[57] = {
                0x01, 0x8c, 0xb3, 0xa0, 0x54, 0xa9, 0x2e, 0xb2, 0x8d, 0xf1, 0xac, 0x29, 0x0a, 0xab, 0x91, 0x5c,
                0x14, 0x64, 0x57, 0xb9, 0xd9, 0xf2, 0x6f, 0x9a, 0xf0, 0xfe, 0xd2, 0xc5, 0x3d, 0x9d, 0xe7, 0xb9,
                0x04, 0x6c, 0xb5, 0x1e, 0x55, 0x3d, 0x26, 0xf9, 0x2e, 0x72, 0x73, 0xe1, 0x5c, 0xa2, 0xfb, 0x41,
                0x4e, 0x6e, 0x72, 0xfe, 0x60, 0xc3, 0x9f, 0x10, 0xda
            };
            own_match(text, buffer(exp, sizeof(exp)));
        };

        "shelley mainnet"_test = [&] {

            should("parse type-00") = [&] {
                uint8_t exp[] = {
                    0x01, 0x94, 0x93, 0x31, 0x5c, 0xd9, 0x2e, 0xb5, 0xd8, 0xc4, 0x30, 0x4e, 0x67, 0xb7, 0xe1, 0x6a,
                    0xe3, 0x6d, 0x61, 0xd3, 0x45, 0x02, 0x69, 0x46, 0x57, 0x81, 0x1a, 0x2c, 0x8e, 0x33, 0x7b, 0x62,
                    0xcf, 0xff, 0x64, 0x03, 0xa0, 0x6a, 0x3a, 0xcb, 0xc3, 0x4f, 0x8c, 0x46, 0x00, 0x3c, 0x69, 0xfe,
                    0x79, 0xa3, 0x62, 0x8c, 0xef, 0xa9, 0xc4, 0x72, 0x51
                };
                auto addr = own_match("addr1qx2fxv2umyhttkxyxp8x0dlpdt3k6cwng5pxj3jhsydzer3n0d3vllmyqwsx5wktcd8cc3sq835lu7drv2xwl2wywfgse35a3x"sv, buffer(exp, sizeof(exp)));
                expect(memcmp(addr.data() + 1, payment_hash.data(), payment_hash.size()) == 0);
                expect(memcmp(addr.data() + 29, stake_hash.data(), stake_hash.size()) == 0);
            };

            should("parse type-01") = [&] {
                uint8_t exp[] = {
                    0x11, 0xc3, 0x7b, 0x1b, 0x5d, 0xc0, 0x66, 0x9f, 0x1d, 0x3c, 0x61, 0xa6, 0xfd, 0xdb, 0x2e, 0x8f,
                    0xde, 0x96, 0xbe, 0x87, 0xb8, 0x81, 0xc6, 0x0b, 0xce, 0x8e, 0x8d, 0x54, 0x2f, 0x33, 0x7b, 0x62,
                    0xcf, 0xff, 0x64, 0x03, 0xa0, 0x6a, 0x3a, 0xcb, 0xc3, 0x4f, 0x8c, 0x46, 0x00, 0x3c, 0x69, 0xfe,
                    0x79, 0xa3, 0x62, 0x8c, 0xef, 0xa9, 0xc4, 0x72, 0x51
                };
                auto addr = own_match("addr1z8phkx6acpnf78fuvxn0mkew3l0fd058hzquvz7w36x4gten0d3vllmyqwsx5wktcd8cc3sq835lu7drv2xwl2wywfgs9yc0hh"sv, buffer(exp, sizeof(exp)));
                expect(memcmp(addr.data() + 1, script_hash.data(), script_hash.size()) == 0);
                expect(memcmp(addr.data() + 29, stake_hash.data(), stake_hash.size()) == 0);
            };

            should("parse type-02") = [&] {
                uint8_t exp[] = {
                    0x21, 0x94, 0x93, 0x31, 0x5c, 0xd9, 0x2e, 0xb5, 0xd8, 0xc4, 0x30, 0x4e, 0x67, 0xb7, 0xe1, 0x6a,
                    0xe3, 0x6d, 0x61, 0xd3, 0x45, 0x02, 0x69, 0x46, 0x57, 0x81, 0x1a, 0x2c, 0x8e, 0xc3, 0x7b, 0x1b,
                    0x5d, 0xc0, 0x66, 0x9f, 0x1d, 0x3c, 0x61, 0xa6, 0xfd, 0xdb, 0x2e, 0x8f, 0xde, 0x96, 0xbe, 0x87,
                    0xb8, 0x81, 0xc6, 0x0b, 0xce, 0x8e, 0x8d, 0x54, 0x2f
                };
                auto addr = own_match("addr1yx2fxv2umyhttkxyxp8x0dlpdt3k6cwng5pxj3jhsydzerkr0vd4msrxnuwnccdxlhdjar77j6lg0wypcc9uar5d2shs2z78ve"sv, buffer(exp, sizeof(exp)));
                expect(memcmp(addr.data() + 1, payment_hash.data(), payment_hash.size()) == 0);
                expect(memcmp(addr.data() + 29, script_hash.data(), script_hash.size()) == 0);
            };

            should("parse type-03") = [&] {
                uint8_t exp[] = {
                    0x31, 0xc3, 0x7b, 0x1b, 0x5d, 0xc0, 0x66, 0x9f, 0x1d, 0x3c, 0x61, 0xa6, 0xfd, 0xdb, 0x2e, 0x8f,
                    0xde, 0x96, 0xbe, 0x87, 0xb8, 0x81, 0xc6, 0x0b, 0xce, 0x8e, 0x8d, 0x54, 0x2f, 0xc3, 0x7b, 0x1b,
                    0x5d, 0xc0, 0x66, 0x9f, 0x1d, 0x3c, 0x61, 0xa6, 0xfd, 0xdb, 0x2e, 0x8f, 0xde, 0x96, 0xbe, 0x87,
                    0xb8, 0x81, 0xc6, 0x0b, 0xce, 0x8e, 0x8d, 0x54, 0x2f
                };
                auto addr = own_match("addr1x8phkx6acpnf78fuvxn0mkew3l0fd058hzquvz7w36x4gt7r0vd4msrxnuwnccdxlhdjar77j6lg0wypcc9uar5d2shskhj42g"sv, buffer(exp, sizeof(exp)));
                expect(memcmp(addr.data() + 1, script_hash.data(), script_hash.size()) == 0);
                expect(memcmp(addr.data() + 29, script_hash.data(), script_hash.size()) == 0);
            };

            should("parse type-04") = [&] {
                uint8_t exp[] = {
                    0x41, 0x94, 0x93, 0x31, 0x5c, 0xd9, 0x2e, 0xb5, 0xd8, 0xc4, 0x30, 0x4e, 0x67, 0xb7, 0xe1, 0x6a,
                    0xe3, 0x6d, 0x61, 0xd3, 0x45, 0x02, 0x69, 0x46, 0x57, 0x81, 0x1a, 0x2c, 0x8e, 0x81, 0x98, 0xbd,
                    0x43, 0x1b, 0x03
                };
                auto addr = own_match("addr1gx2fxv2umyhttkxyxp8x0dlpdt3k6cwng5pxj3jhsydzer5pnz75xxcrzqf96k"sv, buffer(exp, sizeof(exp)));
                expect(memcmp(addr.data() + 1, payment_hash.data(), payment_hash.size()) == 0);
            };

            should("parse type-05") = [&] {
                uint8_t exp[] = {
                    0x51, 0xc3, 0x7b, 0x1b, 0x5d, 0xc0, 0x66, 0x9f, 0x1d, 0x3c, 0x61, 0xa6, 0xfd, 0xdb, 0x2e, 0x8f,
                    0xde, 0x96, 0xbe, 0x87, 0xb8, 0x81, 0xc6, 0x0b, 0xce, 0x8e, 0x8d, 0x54, 0x2f, 0x81, 0x98, 0xbd,
                    0x43, 0x1b, 0x03
                };
                auto addr = own_match("addr128phkx6acpnf78fuvxn0mkew3l0fd058hzquvz7w36x4gtupnz75xxcrtw79hu"sv, buffer(exp, sizeof(exp)));
                expect(memcmp(addr.data() + 1, script_hash.data(), script_hash.size()) == 0);
            };

            should("parse type-06") = [&] {
                uint8_t exp[] = {
                    0x61, 0x94, 0x93, 0x31, 0x5c, 0xd9, 0x2e, 0xb5, 0xd8, 0xc4, 0x30, 0x4e, 0x67, 0xb7, 0xe1, 0x6a,
                    0xe3, 0x6d, 0x61, 0xd3, 0x45, 0x02, 0x69, 0x46, 0x57, 0x81, 0x1a, 0x2c, 0x8e
                };
                auto addr = own_match("addr1vx2fxv2umyhttkxyxp8x0dlpdt3k6cwng5pxj3jhsydzers66hrl8"sv, buffer(exp, sizeof(exp)));
                expect(memcmp(addr.data() + 1, payment_hash.data(), payment_hash.size()) == 0);
            };

            should("parse type-07") = [&] {
                uint8_t exp[] = {
                    0x71, 0xc3, 0x7b, 0x1b, 0x5d, 0xc0, 0x66, 0x9f, 0x1d, 0x3c, 0x61, 0xa6, 0xfd, 0xdb, 0x2e, 0x8f,
                    0xde, 0x96, 0xbe, 0x87, 0xb8, 0x81, 0xc6, 0x0b, 0xce, 0x8e, 0x8d, 0x54, 0x2f
                };
                auto addr = own_match("addr1w8phkx6acpnf78fuvxn0mkew3l0fd058hzquvz7w36x4gtcyjy7wx"sv, buffer(exp, sizeof(exp)));
                expect(memcmp(addr.data() + 1, script_hash.data(), script_hash.size()) == 0);
            };

            should("parse type-14") = [&] {
                uint8_t exp[] = {
                    0xe1, 0x33, 0x7b, 0x62, 0xcf, 0xff, 0x64, 0x03, 0xa0, 0x6a, 0x3a, 0xcb, 0xc3, 0x4f, 0x8c, 0x46,
                    0x00, 0x3c, 0x69, 0xfe, 0x79, 0xa3, 0x62, 0x8c, 0xef, 0xa9, 0xc4, 0x72, 0x51
                };
                auto addr = own_match("stake1uyehkck0lajq8gr28t9uxnuvgcqrc6070x3k9r8048z8y5gh6ffgw"sv, buffer(exp, sizeof(exp)));
                expect(memcmp(addr.data() + 1, stake_hash.data(), stake_hash.size()) == 0);
            };

            should("parse type-15") = [&] {
                uint8_t exp[] = {
                    0xf1, 0xc3, 0x7b, 0x1b, 0x5d, 0xc0, 0x66, 0x9f, 0x1d, 0x3c, 0x61, 0xa6, 0xfd, 0xdb, 0x2e, 0x8f,
                    0xde, 0x96, 0xbe, 0x87, 0xb8, 0x81, 0xc6, 0x0b, 0xce, 0x8e, 0x8d, 0x54, 0x2f
                };
                auto addr = own_match("stake178phkx6acpnf78fuvxn0mkew3l0fd058hzquvz7w36x4gtcccycj5"sv, buffer(exp, sizeof(exp)));
                expect(memcmp(addr.data() + 1, script_hash.data(), script_hash.size()) == 0);
            };
            
        };

        "throws error on wrong chars"_test = [&] {
            expect(throws<error>([] { bech32 addr("stake178phkx6acpnf78fuvxn0mk!ew3l0fd058hzquvz7w36x4gtcccycj5", false); }));
            expect(boost::ut::nothrow([] { bech32 addr("stake178phkx6acpnf78fuvxn0mkew3l0fd058hzquvz7w36x4gtcccycj5", false); }));
        };

        "throws error on unknown prefix"_test = [&] {
            expect(boost::ut::nothrow([] { bech32 addr("stake178phkx6acpnf78fuvxn0mkew3l0fd058hzquvz7w36x4gtcccycj5", true); }));
            expect(throws<error>([] { bech32 addr("new178phkx6acpnf78fuvxn0mk!ew3l0fd058hzquvz7w36x4gtcccycj5", true); }));
        };
    };
};