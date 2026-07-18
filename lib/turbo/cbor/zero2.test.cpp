/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include "zero2.hpp"
#include <turbo/common/file.hpp>
#include <turbo/common/test.hpp>
#include <turbo/config.hpp>

using namespace turbo;
using namespace turbo::cbor;
using namespace turbo::cbor::zero2;

suite cbor_zero2_test = [] {
    "cbor::zero2"_test = [] {
        "no data"_test = [] {
            const auto data = uint8_vector::from_hex("");
            expect(throws([&] { parse(data); }));
        };
        "data_begin"_test = [] {
            const auto data = uint8_vector::from_hex("1B000000FFFFFFFFFF");
            expect(parse(data).get().data_begin() == data.data());
        };
        "data_raw"_test = [] {
            {
                const auto data = uint8_vector::from_hex("18FFFFFFFFFFFF");
                expect_equal(parse(data).get().data_raw(), uint8_vector::from_hex("18FF"));
            }
            {
                const auto data = uint8_vector::from_hex("9F0001FF030405");
                expect_equal(parse(data).get().data_raw(), uint8_vector::from_hex("9F0001FF"));
            }
        };
        "uint"_test = [] {
            expect_equal(parse(uint8_vector::from_hex("00")).get().uint(), 0);
            expect_equal(parse(uint8_vector::from_hex("01")).get().uint(), 1);
            expect_equal(parse(uint8_vector::from_hex("02")).get().uint(), 2);
            expect_equal(parse(uint8_vector::from_hex("03")).get().uint(), 3);
            expect_equal(parse(uint8_vector::from_hex("04")).get().uint(), 4);
            expect_equal(parse(uint8_vector::from_hex("05")).get().uint(), 5);
            expect_equal(parse(uint8_vector::from_hex("06")).get().uint(), 6);
            expect_equal(parse(uint8_vector::from_hex("07")).get().uint(), 7);
            expect_equal(parse(uint8_vector::from_hex("08")).get().uint(), 8);
            expect_equal(parse(uint8_vector::from_hex("09")).get().uint(), 9);
            expect_equal(parse(uint8_vector::from_hex("0A")).get().uint(), 10);
            expect_equal(parse(uint8_vector::from_hex("0B")).get().uint(), 11);
            expect_equal(parse(uint8_vector::from_hex("0C")).get().uint(), 12);
            expect_equal(parse(uint8_vector::from_hex("0D")).get().uint(), 13);
            expect_equal(parse(uint8_vector::from_hex("0E")).get().uint(), 14);
            expect_equal(parse(uint8_vector::from_hex("0F")).get().uint(), 15);
            expect_equal(parse(uint8_vector::from_hex("10")).get().uint(), 16);
            expect_equal(parse(uint8_vector::from_hex("11")).get().uint(), 17);
            expect_equal(parse(uint8_vector::from_hex("12")).get().uint(), 18);
            expect_equal(parse(uint8_vector::from_hex("13")).get().uint(), 19);
            expect_equal(parse(uint8_vector::from_hex("14")).get().uint(), 20);
            expect_equal(parse(uint8_vector::from_hex("15")).get().uint(), 21);
            expect_equal(parse(uint8_vector::from_hex("16")).get().uint(), 22);
            expect_equal(parse(uint8_vector::from_hex("17")).get().uint(), 23);
            {
                const auto data = uint8_vector::from_hex("18FF");
                auto pv = parse(data);
                expect_equal(pv.get().uint(), 0xFFULL);
                expect(pv.get().data_special().data() == data.data() + 2);
                expect_equal(pv.get().data_special().size(), 0xFF);
            }
            {
                const auto data = uint8_vector::from_hex("19FFFF");
                auto pv = parse(data);
                expect_equal(pv.get().uint(), 0xFFFFULL);
                expect(pv.get().data_special().data() == data.data() + 3);
                expect_equal(pv.get().data_special().size(), 0xFFFF);
            }
            {
                const auto data = uint8_vector::from_hex("1AFFFFFFFF");
                auto pv = parse(data);
                expect_equal(pv.get().uint(), 0xFFFFFFFFULL);
                expect(pv.get().data_special().data() == data.data() + 5);
                expect_equal(pv.get().data_special().size(), 0xFFFFFFFF);
            }
            {
                const auto data = uint8_vector::from_hex("1B000000FFFFFFFFFF");
                auto pv = parse(data);
                expect_equal(pv.get().uint(), 0xFFFFFFFFFFULL);
                expect(pv.get().data_special().data() == data.data() + 9);
                expect_equal(pv.get().data_special().size(), 0xFFFFFFFFFFULL);
            }
            {
                const auto data = uint8_vector::from_hex("1B000000FFFFFFFFFF");
                auto pv = parse(data);
                auto &v = pv.get();
                expect(throws([&] { v.text(); }));
                expect(throws([&] { v.bytes(); }));
                expect(throws([&] { v.array(); }));
                expect(throws([&] { v.map(); }));
                expect(throws([&] { v.tag(); }));
                expect(throws([&] { v.special(); }));
            }
        };
        "nint"_test = [] {
            expect_equal(parse(uint8_vector::from_hex("20")).get().nint(), 1);
            expect_equal(parse(uint8_vector::from_hex("21")).get().nint(), 2);
            expect_equal(parse(uint8_vector::from_hex("22")).get().nint(), 3);
            expect_equal(parse(uint8_vector::from_hex("23")).get().nint(), 4);
            expect_equal(parse(uint8_vector::from_hex("24")).get().nint(), 5);
            expect_equal(parse(uint8_vector::from_hex("25")).get().nint(), 6);
            expect_equal(parse(uint8_vector::from_hex("26")).get().nint(), 7);
            expect_equal(parse(uint8_vector::from_hex("27")).get().nint(), 8);
            expect_equal(parse(uint8_vector::from_hex("28")).get().nint(), 9);
            expect_equal(parse(uint8_vector::from_hex("29")).get().nint(), 10);
            expect_equal(parse(uint8_vector::from_hex("2A")).get().nint(), 11);
            expect_equal(parse(uint8_vector::from_hex("2B")).get().nint(), 12);
            expect_equal(parse(uint8_vector::from_hex("2C")).get().nint(), 13);
            expect_equal(parse(uint8_vector::from_hex("2D")).get().nint(), 14);
            expect_equal(parse(uint8_vector::from_hex("2E")).get().nint(), 15);
            expect_equal(parse(uint8_vector::from_hex("2F")).get().nint(), 16);
            expect_equal(parse(uint8_vector::from_hex("30")).get().nint(), 17);
            expect_equal(parse(uint8_vector::from_hex("31")).get().nint(), 18);
            expect_equal(parse(uint8_vector::from_hex("32")).get().nint(), 19);
            expect_equal(parse(uint8_vector::from_hex("33")).get().nint(), 20);
            expect_equal(parse(uint8_vector::from_hex("34")).get().nint(), 21);
            expect_equal(parse(uint8_vector::from_hex("35")).get().nint(), 22);
            expect_equal(parse(uint8_vector::from_hex("36")).get().nint(), 23);
            expect_equal(parse(uint8_vector::from_hex("37")).get().nint(), 24);
            {
                const auto data = uint8_vector::from_hex("38FF");
                expect_equal(parse(data).get().nint(), 0x100ULL);
            }
            {
                const auto data = uint8_vector::from_hex("39FFFF");
                expect_equal(parse(data).get().nint(), 0x10000ULL);
            }
            {
                const auto data = uint8_vector::from_hex("3AFFFFFFFF");
                expect_equal(parse(data).get().nint(), 0x100000000ULL);
            }
            {
                const auto data = uint8_vector::from_hex("3BFFFFFFFFFFFFFFFF");
                expect(throws([&]{ parse(data).get().nint(); }));
            }
            {
                const auto data = uint8_vector::from_hex("3B000000FFFFFFFFFF");
                auto pv = parse(data);
                auto &v = pv.get();
                expect(throws([&] { v.text(); }));
                expect(throws([&] { v.bytes(); }));
                expect(throws([&] { v.array(); }));
                expect(throws([&] { v.map(); }));
                expect(throws([&] { v.tag(); }));
                expect(throws([&] { v.special(); }));
            }
        };
        "bytes"_test = [] {
            expect_equal(parse(uint8_vector::from_hex("40")).get().bytes(), uint8_vector::from_hex(""));
            expect_equal(parse(uint8_vector::from_hex("4100")).get().bytes(), uint8_vector::from_hex("00"));
            expect_equal(parse(uint8_vector::from_hex("420011")).get().bytes(), uint8_vector::from_hex("0011"));
            expect_equal(parse(uint8_vector::from_hex("43001122")).get().bytes(), uint8_vector::from_hex("001122"));
            expect_equal(parse(uint8_vector::from_hex("4400112233")).get().bytes(), uint8_vector::from_hex("00112233"));
            expect_equal(parse(uint8_vector::from_hex("450011223344")).get().bytes(), uint8_vector::from_hex("0011223344"));
            expect_equal(parse(uint8_vector::from_hex("46001122334455")).get().bytes(), uint8_vector::from_hex("001122334455"));
            expect_equal(parse(uint8_vector::from_hex("4700112233445566")).get().bytes(), uint8_vector::from_hex("00112233445566"));
            expect_equal(parse(uint8_vector::from_hex("480011223344556677")).get().bytes(), uint8_vector::from_hex("0011223344556677"));
            expect_equal(parse(uint8_vector::from_hex("49001122334455667788")).get().bytes(), uint8_vector::from_hex("001122334455667788"));
            expect_equal(parse(uint8_vector::from_hex("4A00112233445566778899")).get().bytes(), uint8_vector::from_hex("00112233445566778899"));
            expect_equal(parse(uint8_vector::from_hex("4B00112233445566778899AA")).get().bytes(), uint8_vector::from_hex("00112233445566778899AA"));
            expect_equal(parse(uint8_vector::from_hex("4C00112233445566778899AABB")).get().bytes(), uint8_vector::from_hex("00112233445566778899AABB"));
            expect_equal(parse(uint8_vector::from_hex("4D00112233445566778899AABBCC")).get().bytes(), uint8_vector::from_hex("00112233445566778899AABBCC"));
            expect_equal(parse(uint8_vector::from_hex("4E00112233445566778899AABBCCDD")).get().bytes(), uint8_vector::from_hex("00112233445566778899AABBCCDD"));
            expect_equal(parse(uint8_vector::from_hex("4F00112233445566778899AABBCCDDEE")).get().bytes(), uint8_vector::from_hex("00112233445566778899AABBCCDDEE"));
            expect_equal(parse(uint8_vector::from_hex("5000112233445566778899AABBCCDDEEFF")).get().bytes(), uint8_vector::from_hex("00112233445566778899AABBCCDDEEFF"));
            expect_equal(parse(uint8_vector::from_hex("5100112233445566778899AABBCCDDEEFF00")).get().bytes(), uint8_vector::from_hex("00112233445566778899AABBCCDDEEFF00"));
            expect_equal(parse(uint8_vector::from_hex("5200112233445566778899AABBCCDDEEFF0011")).get().bytes(), uint8_vector::from_hex("00112233445566778899AABBCCDDEEFF0011"));
            expect_equal(parse(uint8_vector::from_hex("5300112233445566778899AABBCCDDEEFF001122")).get().bytes(), uint8_vector::from_hex("00112233445566778899AABBCCDDEEFF001122"));
            expect_equal(parse(uint8_vector::from_hex("5400112233445566778899AABBCCDDEEFF00112233")).get().bytes(), uint8_vector::from_hex("00112233445566778899AABBCCDDEEFF00112233"));
            expect_equal(parse(uint8_vector::from_hex("5500112233445566778899AABBCCDDEEFF0011223344")).get().bytes(), uint8_vector::from_hex("00112233445566778899AABBCCDDEEFF0011223344"));
            expect_equal(parse(uint8_vector::from_hex("5600112233445566778899AABBCCDDEEFF001122334455")).get().bytes(), uint8_vector::from_hex("00112233445566778899AABBCCDDEEFF001122334455"));
            expect_equal(parse(uint8_vector::from_hex("5700112233445566778899AABBCCDDEEFF00112233445566")).get().bytes(), uint8_vector::from_hex("00112233445566778899AABBCCDDEEFF00112233445566"));
            expect_equal(parse(uint8_vector::from_hex("581800112233445566778899AABBCCDDEEFF0011223344556677")).get().bytes(), uint8_vector::from_hex("00112233445566778899AABBCCDDEEFF0011223344556677"));
            expect(throws([&] { parse(uint8_vector::from_hex("5A")); }));
            expect(throws([&] { parse(uint8_vector::from_hex("5B")); }));
            const auto data = uint8_vector::from_hex("43000102");
            {
                expect_equal(parse(data).get().bytes(), uint8_vector::from_hex("000102"));
            }
            {
                uint8_vector res {};
                parse(data).get().to_bytes(res);
                expect_equal(uint8_vector::from_hex("000102"), res);
            }
            {
                auto pv = parse(data);
                auto &v = pv.get();
                expect(throws([&] { v.uint(); }));
                expect(throws([&] { v.nint(); }));
                //expect(throws([&] { v.bigint(); }));
                expect(throws([&] { v.text(); }));
                expect(throws([&] { v.array(); }));
                expect(throws([&] { v.map(); }));
                expect(throws([&] { v.tag(); }));
                expect(throws([&] { v.special(); }));
            }
        };
        "text"_test = [] {
            expect_equal(parse(uint8_vector::from_hex("60")).get().text(), uint8_vector::from_hex("").str());
            expect_equal(parse(uint8_vector::from_hex("6100")).get().text(), uint8_vector::from_hex("00").str());
            expect_equal(parse(uint8_vector::from_hex("620011")).get().text(), uint8_vector::from_hex("0011").str());
            expect_equal(parse(uint8_vector::from_hex("63001122")).get().text(), uint8_vector::from_hex("001122").str());
            expect_equal(parse(uint8_vector::from_hex("6400112233")).get().text(), uint8_vector::from_hex("00112233").str());
            expect_equal(parse(uint8_vector::from_hex("650011223344")).get().text(), uint8_vector::from_hex("0011223344").str());
            expect_equal(parse(uint8_vector::from_hex("66001122334455")).get().text(), uint8_vector::from_hex("001122334455").str());
            expect_equal(parse(uint8_vector::from_hex("6700112233445566")).get().text(), uint8_vector::from_hex("00112233445566").str());
            expect_equal(parse(uint8_vector::from_hex("680011223344556677")).get().text(), uint8_vector::from_hex("0011223344556677").str());
            expect_equal(parse(uint8_vector::from_hex("69001122334455667788")).get().text(), uint8_vector::from_hex("001122334455667788").str());
            expect_equal(parse(uint8_vector::from_hex("6A00112233445566778899")).get().text(), uint8_vector::from_hex("00112233445566778899").str());
            expect_equal(parse(uint8_vector::from_hex("6B00112233445566778899AA")).get().text(), uint8_vector::from_hex("00112233445566778899AA").str());
            expect_equal(parse(uint8_vector::from_hex("6C00112233445566778899AABB")).get().text(), uint8_vector::from_hex("00112233445566778899AABB").str());
            expect_equal(parse(uint8_vector::from_hex("6D00112233445566778899AABBCC")).get().text(), uint8_vector::from_hex("00112233445566778899AABBCC").str());
            expect_equal(parse(uint8_vector::from_hex("6E00112233445566778899AABBCCDD")).get().text(), uint8_vector::from_hex("00112233445566778899AABBCCDD").str());
            expect_equal(parse(uint8_vector::from_hex("6F00112233445566778899AABBCCDDEE")).get().text(), uint8_vector::from_hex("00112233445566778899AABBCCDDEE").str());
            expect_equal(parse(uint8_vector::from_hex("7000112233445566778899AABBCCDDEEFF")).get().text(), uint8_vector::from_hex("00112233445566778899AABBCCDDEEFF").str());
            expect_equal(parse(uint8_vector::from_hex("7100112233445566778899AABBCCDDEEFF00")).get().text(), uint8_vector::from_hex("00112233445566778899AABBCCDDEEFF00").str());
            expect_equal(parse(uint8_vector::from_hex("7200112233445566778899AABBCCDDEEFF0011")).get().text(), uint8_vector::from_hex("00112233445566778899AABBCCDDEEFF0011").str());
            expect_equal(parse(uint8_vector::from_hex("7300112233445566778899AABBCCDDEEFF001122")).get().text(), uint8_vector::from_hex("00112233445566778899AABBCCDDEEFF001122").str());
            expect_equal(parse(uint8_vector::from_hex("7400112233445566778899AABBCCDDEEFF00112233")).get().text(), uint8_vector::from_hex("00112233445566778899AABBCCDDEEFF00112233").str());
            expect_equal(parse(uint8_vector::from_hex("7500112233445566778899AABBCCDDEEFF0011223344")).get().text(), uint8_vector::from_hex("00112233445566778899AABBCCDDEEFF0011223344").str());
            expect_equal(parse(uint8_vector::from_hex("7600112233445566778899AABBCCDDEEFF001122334455")).get().text(), uint8_vector::from_hex("00112233445566778899AABBCCDDEEFF001122334455").str());
            expect_equal(parse(uint8_vector::from_hex("7700112233445566778899AABBCCDDEEFF00112233445566")).get().text(), uint8_vector::from_hex("00112233445566778899AABBCCDDEEFF00112233445566").str());
            expect_equal(parse(uint8_vector::from_hex("781800112233445566778899AABBCCDDEEFF0011223344556677")).get().text(), uint8_vector::from_hex("00112233445566778899AABBCCDDEEFF0011223344556677").str());
            expect(throws([&] { parse(uint8_vector::from_hex("7A")); }));
            expect(throws([&] { parse(uint8_vector::from_hex("7B")); }));
            const auto data = uint8_vector::from_hex("63494A4B");
            {
                expect_equal(std::string { "IJK" }, parse(data).get().text());
            }
            {
                std::string res {};
                parse(data).get().to_text(res);
                expect_equal(std::string { "IJK" }, res);
            }
            {
                auto pv = parse(data);
                auto &v = pv.get();
                expect(throws([&] { v.uint(); }));
                expect(throws([&] { v.nint(); }));
                expect(throws([&] { v.bytes(); }));
                expect(throws([&] { v.array(); }));
                expect(throws([&] { v.map(); }));
                expect(throws([&] { v.tag(); }));
                expect(throws([&] { v.special(); }));
            }
        };
        "float32"_test = [] {
            {
                const auto data = uint8_vector::from_hex("FA21B62E17");
                expect_equal(parse(data).get().float32(), 123.45e-20F);
            }
            {
                const auto data = uint8_vector::from_hex("FAF7F37868");
                expect_equal(parse(data).get().float32(), -9876.33e30F);
            }
            {
                const auto data = uint8_vector::from_hex("FAF7F37868");
                auto pv = parse(data);
                auto &v = pv.get();
                expect(throws([&] { v.uint(); }));
                expect(throws([&] { v.nint(); }));
                expect(throws([&] { v.text(); }));
                expect(throws([&] { v.bytes(); }));
                expect(throws([&] { v.array(); }));
                expect(throws([&] { v.map(); }));
                expect(throws([&] { v.tag(); }));
            }
        };
        "array indefinite"_test = [] {
            const auto data = uint8_vector::from_hex("9f0001ff");
            {
                auto pv = parse(data);
                expect_equal(pv.get().type(), major_type::array);
                auto &it = pv.get().array();
                expect(!it.done());
                {
                    auto &v = it.read();
                    expect_equal(v.uint(), 0ULL);
                }
                {
                    auto &v = it.read();
                    expect_equal(v.uint(), 1ULL);
                }
                expect(it.done());
                //expect(boost::ut::nothrow([&]{ it.read(); }));
                //expect(throws([&]{ it.read(); }));
                const auto buf1 = pv.get().data_raw();
                const auto buf2 = pv.get().data_raw();
                expect_equal(buf1, buf2);
            }
            {
                auto pv = parse(data);
                expect_equal(1, pv.get().at(1).uint());
            }
            {
                auto pv = parse(data);
                auto &v = pv.get();
                expect(throws([&] { v.uint(); }));
                expect(throws([&] { v.nint(); }));
                expect(throws([&] { v.text(); }));
                expect(throws([&] { v.bytes(); }));
                expect(throws([&] { v.float32(); }));
                expect(throws([&] { v.map(); }));
                expect(throws([&] { v.tag(); }));
                expect(throws([&] { v.special(); }));
            }
        };
        "array"_test = [] {
            for (size_t i = 0; i <= 0x17; ++i) {
                uint8_vector data {};
                data.reserve(i + 1);
                data << 0x80 + i;
                for (size_t j = 0; j < i; ++j) {
                    data << j;
                }
                auto pv = parse(data);
                auto &it = pv.get().array();
                for (size_t j = 0; j < i; ++j) {
                    expect(!it.done());
                    expect(it.read().uint() == j);
                }
                expect(it.done());
                const auto buf1 = pv.get().data_raw();
                const auto buf2 = pv.get().data_raw();
                expect_equal(buf1, buf2);
            }
            expect(throws([&] { parse(uint8_vector::from_hex("9B")); }));
            const auto data = uint8_vector::from_hex("820001");
            {
                auto pv = parse(data);
                expect_equal(pv.get().type(), major_type::array);
                auto &it = pv.get().array();
                expect_equal(pv.get().special_uint(), 2ULL);
                expect(!it.done());
                {
                    auto &v = it.read();
                    expect_equal(v.uint(), 0ULL);
                }
                {
                    auto &v = it.read();
                    expect_equal(v.uint(), 1ULL);
                }
                expect(it.done());
                expect(throws([&]{ it.read(); }));
                //expect(throws([&]{ it.done(); }));
            }
            {
                auto pv = parse(data);
                auto &it = pv.get().array();
                it.skip(1);
                expect(!it.done());
                {
                    auto &v = it.read();
                    expect_equal(v.uint(), 1ULL);
                }
            }
            // implicit consume
            {
                expect_equal(3, parse(data).get().data_raw().size());
            }
        };
        "map"_test = [] {
            for (size_t i = 0; i <= 0x17; ++i) {
                uint8_vector data {};
                data.reserve(i + 1);
                data << 0xA0 + i;
                for (size_t j = 0; j < i; ++j) {
                    data << j << j;
                }
                auto pv = parse(data);
                auto &it = pv.get().map();
                for (size_t j = 0; j < i; ++j) {
                    expect(!it.done());
                    auto &k = it.read_key();
                    expect_equal(j, k.uint());
                    expect_equal(j, it.read_val(std::move(k)).uint());
                }
                expect(it.done());
                const auto buf1 = pv.get().data_raw();
                const auto buf2 = pv.get().data_raw();
                expect_equal(buf1, buf2);
            }
            expect(throws([&] { parse(uint8_vector::from_hex("BB")); }));
            const auto data = uint8_vector::from_hex("A10001");
            {
                auto pv = parse(data);
                expect_equal(pv.get().type(), major_type::map);
                auto &it = pv.get().map();
                expect_equal(pv.get().special_uint(), 1ULL);
                expect(!it.done());
                {
                    auto &key = it.read_key();
                    expect_equal(key.uint(), 0ULL);
                    auto &val = it.read_val(std::move(key));
                    expect_equal(val.uint(), 1ULL);
                }
                expect(it.done());
                expect(throws([&]{ it.read_key(); }));
                //expect(throws([&]{ it.done(); }));
            }
            {
                auto pv = parse(data);
                auto &it = pv.get().map();
                expect(!it.done());
                it.skip(1);
                expect(it.done());
            }
            {
                auto pv = parse(data);
                auto &v = pv.get();
                expect(throws([&] { v.uint(); }));
                expect(throws([&] { v.nint(); }));
                expect(throws([&] { v.text(); }));
                expect(throws([&] { v.bytes(); }));
                expect(throws([&] { v.float32(); }));
                expect(throws([&] { v.array(); }));
                expect(throws([&] { v.tag(); }));
                expect(throws([&] { v.special(); }));
            }
            // implicit consume
            {
                expect_equal(3, parse(data).get().data_raw().size());
            }
        };
        "map indefinite"_test = [] {
            const auto data = uint8_vector::from_hex("BF0001FF");
            auto pv = parse(data);
            expect_equal(pv.get().type(), major_type::map);
            auto &it = pv.get().map();
            expect(!it.done());
            {
                auto &key = it.read_key();
                expect_equal(key.uint(), 0ULL);
                auto &val = it.read_val(std::move(key));
                expect_equal(val.uint(), 1ULL);
            }
            expect(it.done());
            //expect(boost::ut::nothrow([&]{ it.read_key(); }));
            //expect(throws([&]{ it.read_key(); }));
            const auto buf1 = pv.get().data_raw();
            const auto buf2 = pv.get().data_raw();
            expect_equal(buf1, buf2);
        };
        "tag"_test = [] {
            for (size_t i = 0; i <= 0x17; ++i) {
                uint8_vector data {};
                data.reserve(i + 1);
                data << 0xC0 + i;
                data << i << i;
                auto pv = parse(data);
                auto &t = pv.get().tag();
                expect_equal(i, t.id());
                expect_equal(i, t.read().uint());
                const auto buf1 = pv.get().data_raw();
                const auto buf2 = pv.get().data_raw();
                expect_equal(buf1, buf2);
            }
            expect(throws([&] { parse(uint8_vector::from_hex("DA")); }));
            expect(throws([&] { parse(uint8_vector::from_hex("DB")); }));
            {
                const auto data = uint8_vector::from_hex("C102");
                auto pv1 = parse(data);
                expect_equal(pv1.get().type(), major_type::tag);
                auto &kv1 = pv1.get().tag();
                expect_equal(kv1.id(), 1ULL);
                expect_equal(kv1.read().uint(), 2ULL);
            }
            {
                const auto data = uint8_vector::from_hex("9fC200C201ff");
                auto pv = parse(data);
                auto &it = pv.get().array();
                expect_equal(0, it.read().tag().read().uint());
                expect_equal(1, it.read().tag().read().uint());
                expect(it.done());
            }
            const auto data = uint8_vector::from_hex("C29f0001ff");
            {
                auto pv1 = parse(data);
                expect_equal(pv1.get().type(), major_type::tag);
                auto &kv1 = pv1.get().tag();
                expect_equal(kv1.id(), 2ULL);
                auto &vv = kv1.read();
                expect_equal(vv.type(), major_type::array);
                auto &it = vv.array();
                expect(!it.done());
                {
                    auto &v = it.read();
                    expect_equal(v.uint(), 0ULL);
                }
                {
                    auto &v = it.read();
                    expect_equal(v.uint(), 1ULL);
                }
                expect(it.done());
            }
            // implicit consume
            {
                expect_equal(5, parse(data).get().data_raw().size());
            }
            {
                auto pv = parse(data);
                auto &v = pv.get();
                expect(throws([&] { v.uint(); }));
                expect(throws([&] { v.nint(); }));
                expect(throws([&] { v.text(); }));
                expect(throws([&] { v.bytes(); }));
                expect(throws([&] { v.float32(); }));
                expect(throws([&] { v.map(); }));
                expect(throws([&] { v.array(); }));
                expect(throws([&] { v.special(); }));
            }
        };
        "special"_test = [] {
            for (size_t i = 0; i <= 0x13; ++i) {
                uint8_vector data {};
                data.reserve(1);
                data << 0xE0 + i;
                expect(throws([&] { parse(data); }));
            }
            expect_equal(parse(uint8_vector::from_hex("F4")).get().special(), special_val::s_false);
            expect_equal(parse(uint8_vector::from_hex("F5")).get().special(), special_val::s_true);
            expect_equal(parse(uint8_vector::from_hex("F6")).get().special(), special_val::s_null);
            expect_equal(parse(uint8_vector::from_hex("F7")).get().special(), special_val::s_undefined);
            expect(throws([&] { parse(uint8_vector::from_hex("F8")); }));
            expect(throws([&] { parse(uint8_vector::from_hex("F9")); }));
            expect(throws([&] { parse(uint8_vector::from_hex("FB")); }));
            {
                const auto data = uint8_vector::from_hex("FF");
                {
                    decoder dec { data };
                    expect(!dec.done());
                    auto &v = dec.read();
                    expect_equal(v.special(), special_val::s_break);
                    expect(v.type_byte() == 0xFF);
                    expect(dec.done());
                }
                {
                    auto pv = parse(data);
                    auto &v = pv.get();
                    expect(throws([&] { v.uint(); }));
                    expect(throws([&] { v.nint(); }));
                    expect(throws([&] { v.text(); }));
                    expect(throws([&] { v.bytes(); }));
                    expect(throws([&] { v.float32(); }));
                    expect(throws([&] { v.tag(); }));
                    expect(throws([&] { v.map(); }));
                    expect(throws([&] { v.array(); }));
                }
            }
            {
                const auto data = uint8_vector::from_hex("F6");
                auto pv = cbor::zero2::parse(data);
                auto &v = pv.get();
                expect_equal(v.special(), special_val::s_null);
                expect(v.is_null());
                expect(v.type_byte() == 0xF6);
            }
        };
        "chunked_text"_test = [] {
            {
                static const auto bytes = uint8_vector::from_hex("7F624142624344FF");
                {
                    auto pv = parse(bytes);
                    std::string res {};
                    pv.get().to_text(res);
                    expect_equal(std::string { "ABCD" }, res);
                    expect_equal(8, pv.get().data_raw().size());
                }
                {
                    auto pv = parse(bytes);
                    expect_equal(8, pv.get().data_raw().size());
                }
                {
                    expect(throws([&] { parse(bytes).get().text(); }));
                }
                {
                    uint8_vector res {};
                    expect(throws([&] { parse(bytes).get().to_bytes(res); }));
                }
                // test the implicit consumption
                {
                    expect_equal(8, parse(bytes).get().data_raw().size());
                }
            }
            // to many chunks
            {
                uint8_vector data {};
                data << 0x7F;
                for (size_t i = 0; i < 1025; ++i) {
                    data << 0x61 << 0x65;
                }
                data << 0xFF;
                expect_equal(1025 * 2 + 2, data.size());
                expect(throws([&] {
                    std::string res {};
                    parse(data).get().to_text(res);
                }));
            }
        };
        "chunked_bytes"_test = [] {
            {
                uint8_vector data {};
                data << 0x5F;
                data << 0x42 << uint8_vector::from_hex("0011");
                data << 0x42 << uint8_vector::from_hex("2233");
                data << 0xFF;
                {
                    auto pv = parse(data);
                    uint8_vector res {};
                    pv.get().to_bytes(res);
                    expect_equal(data.size(), pv.get().data_raw().size());
                    expect_equal(uint8_vector::from_hex("00112233"), res);
                }
                {
                    auto pv = parse(data);
                    expect_equal(8, pv.get().data_raw().size());
                }
                {
                    expect(throws([&] { parse(data).get().bytes(); }));
                }
                {
                    std::string res {};
                    expect(throws([&] { parse(data).get().to_text(res); }));
                }
                // test the implicit consumption
                {
                    expect_equal(8, parse(data).get().data_raw().size());
                }
            }
            // to many chunks
            {
                uint8_vector data {};
                data << 0x5F;
                for (size_t i = 0; i < 1025; ++i) {
                    data << 0x41 << 0x00;
                }
                data << 0xFF;
                expect_equal(1025 * 2 + 2, data.size());
                expect(throws([&] {
                    uint8_vector res {};
                    parse(data).get().to_bytes(res);
                }));
            }
        };
        "extract"_test = [] {
            {
                static const auto data = uint8_vector::from_hex("82820001820203");
                {
                    auto pv = parse(data);
                    expect_equal(3, extract(pv.get(), std::initializer_list<size_t> { 1, 1 }).uint());
                }
                {
                    auto pv = parse(data);
                    expect(throws([&] { extract(pv.get(), std::initializer_list<size_t> { 2, 1 }); }));
                }
                {
                    auto pv = parse(data);
                    expect(throws([&] { extract(pv.get(), std::initializer_list<size_t> { 1, 2 }); }));
                }
                {
                    auto pv = parse(data);
                    expect(throws([&] { extract(pv.get(), std::initializer_list<size_t> { 1, 1, 1 }); }));
                }
            }
            {
                static const auto data = uint8_vector::from_hex("A20AA2000001010BA202020303");
                {
                    auto pv = parse(data);
                    expect_equal(3, extract(pv.get(), std::initializer_list<size_t> { 1, 1 }).uint());
                }
                {
                    auto pv = parse(data);
                    expect(throws([&] { extract(pv.get(), std::initializer_list<size_t> { 2, 1 }); }));
                }
                {
                    auto pv = parse(data);
                    expect(throws([&] { extract(pv.get(), std::initializer_list<size_t> { 1, 2 }); }));
                }
                {
                    auto pv = parse(data);
                    expect(throws([&] { extract(pv.get(), std::initializer_list<size_t> { 1, 1, 1 }); }));
                }
            }
            {
                expect(throws([&] { extract(parse(uint8_vector::from_hex("FF")).get(), std::initializer_list<size_t> { 2, 1 }); }));
            }
        };
        "indefinite"_test = [] {
            for (const auto &[hex, exp]: std::initializer_list<std::pair<std::string_view, bool>> {
                { "64494A4B4C", false },
                { "7F6149614AFF", true },
                { "420011", false },
                { "5F4149414AFF", true },
                { "A10001", false },
                { "BF0001FF", true },
                { "9f0001ff" , true },
                { "820001" , false }
            }) {
                const auto data = uint8_vector::from_hex(hex);
                expect_equal(exp, parse(data).get().indefinite());
            }
            for (const auto hex: std::initializer_list<std::string_view> {
                { "00" },
                { "FF" }
            }) {
                const auto data = uint8_vector::from_hex(hex);
                expect(throws([&]{ parse(data).get().indefinite(); }));
            }
        };
        "equality"_test = [] {
            auto a = uint8_vector::from_hex("00");
            auto b = uint8_vector::from_hex("00");
            auto c = uint8_vector::from_hex("01");
            expect(parse(a).get().data_raw() == parse(b).get().data_raw());
            expect(parse(a).get().data_raw() != parse(c).get().data_raw());
            expect(parse(b).get().data_raw() != parse(c).get().data_raw());
        };
        "order"_test = [] {
            const auto a = uint8_vector::from_hex("00FF");
            const auto b = uint8_vector::from_hex("0102");
            expect(parse(a).get().data_raw() < parse(b).get().data_raw());
            expect(!(parse(a).get().data_raw() < parse(a).get().data_raw()));
            expect(!(parse(b).get().data_raw() < parse(a).get().data_raw()));
            expect(!(parse(b).get().data_raw() < parse(b).get().data_raw()));
        };
        "stringify"_test = [] {
            for (const auto &[hex, exp]: std::initializer_list<std::pair<std::string_view, std::string_view>> {
                { "3903e7", "I -1000" },
                { "64494A4B4C", "T 'IJKL'" },
                { "7F6149614AFF", "T chunked 'IJ'" },
                { "420011", "B #0011" },
                { "5F4149414AFF", "B chunked #494A ('IJ')" },
                { "A10001", "{\n    #0: I 0: I 1\n}(size: 1)" },
                { "A3000102030405", "{\n    #0: I 0: I 1\n    #1: I 2: I 3\n    ...\n}(size: 3)" },
                { "BF0001FF", "{\n    #0: I 0: I 1\n}(unbounded size: 1)" },
                { "C29f0001ff" , "TAG 2 [\n    #0: I 0\n    #1: I 1\n](unbounded size: 2)" },
                { "C2820001" , "TAG 2 [\n    #0: I 0\n    #1: I 1\n](size: 2)" },
                { "C283000102" , "TAG 2 [\n    #0: I 0\n    #1: I 1\n    ...\n](size: 3)" },
                { "FF" , "break" },
                { "FA00000000" , "F32 0" }
            }) {
                expect_equal(parse(uint8_vector::from_hex(hex)).get().to_string(2), exp);
                std::ostringstream os {};
                parse(uint8_vector::from_hex(hex)).get().to_stream(os, 2);
                expect_equal(os.str(), exp);
            }
        };
        "real data"_test = [] {
            for (const char *rel_path: { "data/conway/block-0.cbor" }) {
                const auto path = install_path(rel_path);
                const auto bytes = file::read(path);
                expect(boost::ut::nothrow([&] {
                    cbor::zero2::decoder dec { bytes };
                    while (!dec.done()) {
                        dec.read();
                    }
                }));
            }
        };
    };
};
