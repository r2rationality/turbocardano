/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include <numeric>
#include "bytes.hpp"
#include "test.hpp"

namespace {
    using namespace turbo;
}

suite turbo_common_bytes_suite = [] {
    "turbo::common::bytes"_test = [] {
        "buffer"_test = [] {
            const byte_array<4> tmp { 0x01, 0x02, 0x03, 0x04 };
            expect_equal(0x04030201U, static_cast<buffer>(tmp).to<uint32_t>());
            expect_equal(0x04U, static_cast<buffer>(tmp).at(3));
            expect_equal(0x0302U, static_cast<buffer>(tmp).subbuf(1,2).to<uint16_t>());
            expect_equal(0x04U, static_cast<buffer>(tmp).subbuf(3).to<uint8_t>());
            expect_equal(0U, static_cast<buffer>(tmp).subbuf(4).size());
            expect_equal(0U, static_cast<buffer>(tmp).subbuf(4, 0).size());
            expect(throws([&] { static_cast<buffer>(tmp).to<uint64_t>(); }));
            expect(throws([&] { static_cast<buffer>(tmp).at(4); }));
            expect(throws([&] { static_cast<buffer>(tmp).subbuf(5); }));
            expect(throws([&] { static_cast<buffer>(tmp).subbuf(5, 0); }));
        };
        "byte_array contruct"_test = [] {
            expect(throws([] { const byte_array<4> tmp { 5, 4, 3, 2, 5 }; }));
            expect(throws([] { const byte_array<4> tmp { 5, 4, 3 }; }));
            expect(throws([] { const byte_array<4> tmp { buffer { nullptr, 0} }; }));
            expect(throws([] { const byte_array<4> tmp { std::string_view { nullptr, 0} }; }));
        };
        "byte_array assign"_test = [] {
            byte_array<4> a {};
            expect(throws([&] { a = buffer { nullptr, 0 }; }));
            expect(throws([&] { a = std::string_view { nullptr, 0}; }));
            a = std::string_view { "\x01\x02\x03\x04", 4 };
            expect_equal(0x04030201U, static_cast<buffer>(a).to<uint32_t>());
        };
        "byte_array bit"_test = [] {
            byte_array<2> a { 0x85U, 0x10U };
            expect_equal(true, a.bit(0));
            expect_equal(false, a.bit(1));
            expect_equal(false, a.bit(2));
            expect_equal(false, a.bit(3));
            expect_equal(false, a.bit(4));
            expect_equal(true, a.bit(5));
            expect_equal(false, a.bit(6));
            expect_equal(true, a.bit(7));
            expect_equal(false, a.bit(8));
            expect_equal(false, a.bit(9));
            expect_equal(false, a.bit(10));
            expect_equal(true, a.bit(11));
            expect_equal(false, a.bit(12));
            expect_equal(false, a.bit(13));
            expect_equal(false, a.bit(14));
            expect_equal(false, a.bit(15));
            expect(throws([&] { a.bit(16); }));
        };
        "do not initialize"_test = [] {
            using my_array_t = byte_array<4>;
            my_array_t storage { 5, 4, 3, 2 };
            my_array_t *a = new (&storage) my_array_t;
            expect(std::accumulate(a->begin(), a->end(), 0U) != 0U);
            // It's ok to not call a's destructor here

        };
        "initialize with zeros"_test = [] {
            byte_array<4> a {};
            expect(a.size() == 4);
            for (auto v: a)
                expect(v == 0) << v;
        };
        "initialize with values"_test = [] {
            const byte_array<4> a { 1, 2, 3, 4 };
            expect(a.size() == 4);
            expect_equal(1, a[0]);
            expect_equal(2, a[1]);
            expect_equal(3, a[2]);
            expect_equal(4, a[3]);
        };
        "construct from a span"_test = [] {
            const byte_array<4> b { 9, 8, 7, 6 };
            const byte_array<4> c { std::span(b) };
            expect_equal(b, c);
        };
        "construct from a string_view"_test = [] {
            using namespace std::literals;
            byte_array<4> a { "\x01\x02\x03\x04"sv };
            expect(a.size() == 4);
            expect_equal(1, a[0]);
            expect_equal(2, a[1]);
            expect_equal(3, a[2]);
            expect_equal(4, a[3]);
        };
        "uint_from_oct"_test = [] {
            expect_equal(1, uint_from_oct('1'));
            expect(throws([&] { uint_from_oct('8'); }));
            expect(throws([&] { uint_from_oct('a'); }));
        };
        "construct from hex"_test = [] {
            using namespace std::literals;
            const auto a = byte_array<4>::from_hex("01020304");
            expect(a.size() == 4);
            expect_equal(1, a[0]);
            expect_equal(2, a[1]);
            expect_equal(3, a[2]);
            expect_equal(4, a[3]);
            expect(throws([&] { byte_array<4>::from_hex("01020304050x"); }));
            expect(throws([&] { byte_array<4>::from_hex("0102030405x0"); }));
            expect_equal(uint8_vector::from_hex("01020304"sv), uint8_vector::from_hex("0x01020304"sv));
        };
        "operator<<"_test = [] {
            using namespace std::string_view_literals;
            uint8_vector a {};
            a << 0x22;
            expect_equal("\x22", a.str());
            a << "\x33\x44"sv;
            expect_equal("\x22\x33\x44", a.str());
        };
        "assign_span"_test = [] {
            byte_array<4> a { 1, 2, 3, 4 };
            byte_array<4> b { 9, 8, 7, 6 };
            expect(a.size() == 4);
            expect(a[0] == 1);
            expect(a[1] == 2);
            expect(a[2] == 3);
            expect(a[3] == 4);
            a = std::span(b);
            expect(a[0] == 9);
            expect(a[1] == 8);
            expect(a[2] == 7);
            expect(a[3] == 6);
        };
        "assign_string_view"_test = [] {
            using namespace std::literals;
            byte_array<4> a {};
            expect(a.size() == 4);
            for (const auto v: a) expect(v == 0);
            a = "\x01\x02\x03\x04"sv;
            expect(a[0] == 1);
            expect(a[1] == 2);
            expect(a[2] == 3);
            expect(a[3] == 4);
        };
        "string formatting support"_test = [] {
            auto data = byte_array<4>::from_hex("f0e1d2c3");
            expect(fmt::format("{}", data) == "F0E1D2C3");
        };
        "secure_array"_test = [] {
            using my_sec_array_t = secure_byte_array<4>;
            using my_sec_storage_t = std::aligned_storage<sizeof(my_sec_array_t), alignof(my_sec_array_t)>::type;
            const auto empty = byte_array<4>::from_hex("00000000");
            const auto filled = byte_array<4>::from_hex("DEADBEAF");
            my_sec_storage_t storage {};
            my_sec_array_t *sec = new (&storage) my_sec_array_t { filled };
            const auto *data_ptr = sec->data();
            expect_equal(true, memcmp(data_ptr, filled.data(), filled.size()) == 0);
            sec->~secure_byte_array();
            expect_equal(false, memcmp(data_ptr, filled.data(), filled.size()) == 0);
            expect_equal(true, memcmp(data_ptr, empty.data(), empty.size()) == 0);
        };
    };  
};