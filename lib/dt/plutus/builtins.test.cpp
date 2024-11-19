/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2024 Alex Sierkov (alex dot sierkov at gmail dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/plutus/builtins.hpp>
#include <dt/test.hpp>
#include <dt/blake2b.hpp>
#include <dt/crypto/sha2.hpp>
#include <dt/crypto/sha3.hpp>
#include <dt/ed25519.hpp>

using namespace daedalus_turbo;
using namespace daedalus_turbo::crypto;
using namespace daedalus_turbo::plutus;
using namespace daedalus_turbo::plutus::builtins;

suite plutus_builtins_suite = [] {
    using constant = plutus::constant;
    "plutus::builtins"_test = [] {
        "v1"_test = [] {
            allocator alloc {};
            using namespace std::literals::string_view_literals;
            "add_integer"_test = [&] () mutable {
                expect(add_integer(alloc, { alloc, 2 }, { alloc, 3 }) == value { alloc, 5 });
            };
            "subtract_integer"_test = [&] {
                expect(subtract_integer(alloc, { alloc, -5 }, { alloc, -15 }) == value { alloc, 10 });
            };
            "multiply_integer"_test = [&] {
                expect(multiply_integer(alloc, { alloc, -5 }, { alloc, -5 }) == value { alloc, 25 });
            };
            "divide_integer"_test = [&] {
                expect(divide_integer(alloc, { alloc, 5 }, { alloc, 3 }) == value { alloc, 1 });
                test_same(value { alloc, -2 }, divide_integer(alloc, { alloc, -5 }, { alloc, 3 }));
                test_same(value { alloc, 1 }, divide_integer(alloc, { alloc, -5 }, { alloc, -3 }));
                expect(divide_integer(alloc, { alloc, 5 }, { alloc, -3 }) == value { alloc, -2 });
                expect(throws([&] { divide_integer(alloc, { alloc, 3 }, { alloc, 0 }); }));
            };
            "quotient_integer"_test = [&] {
                expect(quotient_integer(alloc, { alloc, 5 }, { alloc, 3 }) == value { alloc, 1 });
                expect(quotient_integer(alloc, { alloc, -5 }, { alloc, 3 }) == value { alloc, -1 });
                expect(quotient_integer(alloc, { alloc, -5 }, { alloc, -3 }) == value { alloc, 1 });
                expect(quotient_integer(alloc, { alloc, 5 }, { alloc, -3 }) == value { alloc, -1 });
                expect(throws([&] { quotient_integer(alloc, { alloc, 3 }, { alloc, 0 }); }));
            };
            "mod_integer"_test = [&] {
                expect(mod_integer(alloc, { alloc, 5 }, { alloc, 3 }) == value { alloc, 2 });
                expect(mod_integer(alloc, { alloc, -5 }, { alloc, 3 }) == value { alloc, 1 });
                expect(mod_integer(alloc, { alloc, -5 }, { alloc, -3 }) == value { alloc, -2 });
                expect(mod_integer(alloc, { alloc, 5 }, { alloc, -3 }) == value { alloc, -1 });
                expect(throws([&] { mod_integer(alloc, { alloc, 3 }, { alloc, 0 }); }));
            };
            "remainder_integer"_test = [&] {
                expect(remainder_integer(alloc, { alloc, 5 }, { alloc, 3 }) == value { alloc, 2 });
                expect(remainder_integer(alloc, { alloc, -5 }, { alloc, 3 }) == value { alloc, -2 });
                expect(remainder_integer(alloc, { alloc, -5 }, { alloc, -3 }) == value { alloc, -2 });
                expect(remainder_integer(alloc, { alloc, 5 }, { alloc, -3 }) == value { alloc, 2 });
                expect(throws([&] { remainder_integer(alloc, { alloc, 3 }, { alloc, 0 }); }));
            };
            "equals_integer"_test = [&] {
                expect(equals_integer(alloc, { alloc, 5 }, { alloc, 5 }).as_bool());
                expect(!equals_integer(alloc, { alloc, 5 }, { alloc, -5 }).as_bool());
                expect(equals_integer(alloc, { alloc, -5 }, { alloc, -5 }).as_bool());
                expect(!equals_integer(alloc, { alloc, -5 }, { alloc, 5 }).as_bool());
            };
            "less_than_integer"_test = [&] {
                expect(less_than_integer(alloc, { alloc, -5 }, { alloc, 5 }).as_bool());
                expect(!less_than_integer(alloc, { alloc, 5 }, { alloc, -5 }).as_bool());
                expect(!less_than_integer(alloc, { alloc, 5 }, { alloc, 5 }).as_bool());
            };
            "less_than_equals_integer"_test = [&] {
                expect(less_than_equals_integer(alloc, { alloc, -5 }, { alloc, 5 }).as_bool());
                expect(!less_than_equals_integer(alloc, { alloc, 5 }, { alloc, -5 }).as_bool());
                expect(less_than_equals_integer(alloc, { alloc, 5 }, { alloc, 5 }).as_bool());
            };
            "less_than_equals_byte_string"_test = [&] {
                expect(less_than_equals_byte_string(alloc, { alloc, bstr_type::from_hex(alloc, "") }, { alloc, bstr_type::from_hex(alloc, "") }).as_bool());
                expect(less_than_equals_byte_string(alloc, { alloc, bstr_type::from_hex(alloc, "") }, { alloc, bstr_type::from_hex(alloc, "AA") }).as_bool());
                expect(!less_than_equals_byte_string(alloc, { alloc, bstr_type::from_hex(alloc, "AA") }, { alloc, bstr_type::from_hex(alloc, "") }).as_bool());
                expect(less_than_equals_byte_string(alloc, { alloc, bstr_type::from_hex(alloc, "AA") }, { alloc, bstr_type::from_hex(alloc, "AA") }).as_bool());
                expect(less_than_equals_byte_string(alloc, { alloc, bstr_type::from_hex(alloc, "AABB") }, { alloc, bstr_type::from_hex(alloc, "BBAA") }).as_bool());
                expect(!less_than_equals_byte_string(alloc, { alloc, bstr_type::from_hex(alloc, "BBAA") }, { alloc, bstr_type::from_hex(alloc, "AABB") }).as_bool());
                expect(less_than_equals_byte_string(alloc, { alloc, bstr_type::from_hex(alloc, "AABB") }, { alloc, bstr_type::from_hex(alloc, "AABBCC") }).as_bool());
            };
            "append_byte_string"_test = [&] {
                expect(append_byte_string(alloc, { alloc, bstr_type::from_hex(alloc, "") },
                        { alloc, bstr_type::from_hex(alloc, "AA") }).as_bstr() == bstr_type::from_hex(alloc, "AA"));
                expect(append_byte_string(alloc, { alloc, bstr_type::from_hex(alloc, "AA") },
                        { alloc, bstr_type::from_hex(alloc, "") }).as_bstr() == bstr_type::from_hex(alloc, "AA"));
                expect(append_byte_string(alloc, { alloc, bstr_type::from_hex(alloc, "11") },
                        { alloc, bstr_type::from_hex(alloc, "2233") }).as_bstr() == bstr_type::from_hex(alloc, "112233"));
            };
            "cons_byte_string"_test = [&] {
                expect(cons_byte_string(alloc, { alloc, 0x41 },
                        { alloc, bstr_type::from_hex(alloc, "42") }).as_bstr() == bstr_type::from_hex(alloc, "4142"));
                expect(cons_byte_string(alloc, { alloc, 0x42 },
                        { alloc, bstr_type::from_hex(alloc, "") }).as_bstr() == bstr_type::from_hex(alloc, "42"));
                test_same(bstr_type { alloc, uint8_vector::from_hex("FF") }, cons_byte_string(alloc, { alloc, -1 }, { alloc, bstr_type::from_hex(alloc, "") }).as_bstr());
                test_same(bstr_type { alloc, uint8_vector::from_hex("00") }, cons_byte_string(alloc, { alloc, 256 }, { alloc, bstr_type::from_hex(alloc, "") }).as_bstr());
            };
            "cons_byte_string_v2"_test = [&] {
                expect(cons_byte_string_v2(alloc, { alloc, 0x41 },
                        { alloc, bstr_type::from_hex(alloc, "42") }).as_bstr() == bstr_type::from_hex(alloc, "4142"));
                expect(cons_byte_string_v2(alloc, { alloc, 0x42 },
                        { alloc, bstr_type::from_hex(alloc, "") }).as_bstr() == bstr_type::from_hex(alloc, "42"));
                expect(throws([&] { cons_byte_string_v2(alloc, { alloc, -1 }, { alloc, bstr_type::from_hex(alloc, "") }); }));
                expect(throws([&] { cons_byte_string_v2(alloc, { alloc, 256 }, { alloc, bstr_type::from_hex(alloc, "") }); }));
            };
            "slice_byte_string"_test = [&] {
                expect(slice_byte_string(alloc, { alloc, -10 }, { alloc, 2 },
                    { alloc, bstr_type::from_hex(alloc, "0011223344") }).as_bstr() == bstr_type::from_hex(alloc, "0011"));
                expect(slice_byte_string(alloc, { alloc, 2 }, { alloc, -1 },
                    { alloc, bstr_type::from_hex(alloc, "0011223344") }).as_bstr() == bstr_type::from_hex(alloc, ""));
                expect(slice_byte_string(alloc, { alloc, 2 }, { alloc, 10 },
                    { alloc, bstr_type::from_hex(alloc, "0011223344") }).as_bstr() == bstr_type::from_hex(alloc, "223344"));
                expect(slice_byte_string(alloc, { alloc, 20 }, { alloc, 10 },
                   { alloc, bstr_type::from_hex(alloc, "0011223344") }).as_bstr() == bstr_type::from_hex(alloc, ""));
            };
            "length_of_byte_string"_test = [&] {
                expect(length_of_byte_string(alloc, { alloc, bstr_type::from_hex(alloc, "") }).as_int() == 0);
                expect(length_of_byte_string(alloc, { alloc, bstr_type::from_hex(alloc, "0011223344") }).as_int() == 5);
            };
            "index_byte_string"_test = [&] {
                expect(index_byte_string(alloc, { alloc, bstr_type::from_hex(alloc, "0011223344") }, { alloc, 0 }).as_int() == 0x00);
                expect(index_byte_string(alloc, { alloc, bstr_type::from_hex(alloc, "0011223344") }, { alloc, 2 }).as_int() == 0x22);
                expect(index_byte_string(alloc, { alloc, bstr_type::from_hex(alloc, "0011223344") }, { alloc, 4 }).as_int() == 0x44);
                expect(throws([&] { index_byte_string(alloc, { alloc, bstr_type::from_hex(alloc, "0011223344") }, { alloc, -1 }); }));
                expect(throws([&] { index_byte_string(alloc, { alloc, bstr_type::from_hex(alloc, "0011223344") }, { alloc, 5 }); }));
                expect(throws([&] { index_byte_string(alloc, { alloc, bstr_type::from_hex(alloc, "0011223344") }, { alloc, cpp_int { std::numeric_limits<size_t>::max() } + 1 }); }));
            };
            "equals_byte_string"_test = [&] {
                expect(equals_byte_string(alloc, { alloc, bstr_type::from_hex(alloc, "") }, { alloc, bstr_type::from_hex(alloc, "") }).as_bool());
                expect(!equals_byte_string(alloc, { alloc, bstr_type::from_hex(alloc, "") }, { alloc, bstr_type::from_hex(alloc, "AA") }).as_bool());
                expect(!equals_byte_string(alloc, { alloc, bstr_type::from_hex(alloc, "BB") }, { alloc, bstr_type::from_hex(alloc, "") }).as_bool());
                expect(equals_byte_string(alloc, { alloc, bstr_type::from_hex(alloc, "AABB") }, { alloc, bstr_type::from_hex(alloc, "AABB") }).as_bool());
                expect(!equals_byte_string(alloc, { alloc, bstr_type::from_hex(alloc, "AABB") }, { alloc, bstr_type::from_hex(alloc, "AABBCC") }).as_bool());
            };
            "less_than_byte_string"_test = [&] {
                expect(less_than_byte_string(alloc, { alloc, bstr_type::from_hex(alloc, "") }, { alloc, bstr_type::from_hex(alloc, "AA") }).as_bool());
                expect(!less_than_byte_string(alloc, { alloc, bstr_type::from_hex(alloc, "") }, { alloc, bstr_type::from_hex(alloc, "") }).as_bool());
                expect(!less_than_byte_string(alloc, { alloc, bstr_type::from_hex(alloc, "AA") }, { alloc, bstr_type::from_hex(alloc, "AA") }).as_bool());
                expect(less_than_byte_string(alloc, { alloc, bstr_type::from_hex(alloc, "AA") }, { alloc, bstr_type::from_hex(alloc, "AABB") }).as_bool());
            };
            "less_than_equals_byte_string"_test = [&] {
                expect(less_than_equals_byte_string(alloc, { alloc, bstr_type::from_hex(alloc, "") }, { alloc, bstr_type::from_hex(alloc, "AA") }).as_bool());
                expect(!less_than_equals_byte_string(alloc, { alloc, bstr_type::from_hex(alloc, "AA") }, { alloc, bstr_type::from_hex(alloc, "") }).as_bool());
                expect(less_than_equals_byte_string(alloc, { alloc, bstr_type::from_hex(alloc, "") }, { alloc, bstr_type::from_hex(alloc, "") }).as_bool());
                expect(less_than_equals_byte_string(alloc, { alloc, bstr_type::from_hex(alloc, "AA") }, { alloc, bstr_type::from_hex(alloc, "AA") }).as_bool());
                expect(less_than_equals_byte_string(alloc, { alloc, bstr_type::from_hex(alloc, "AA") }, { alloc, bstr_type::from_hex(alloc, "AABB") }).as_bool());
                expect(!less_than_equals_byte_string(alloc, { alloc, bstr_type::from_hex(alloc, "AABB") }, { alloc, bstr_type::from_hex(alloc, "AA") }).as_bool());
            };
            "sha2_256"_test = [&] {
                {
                    const value exp { alloc, sha2::hash_256::from_hex("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") };
                    const auto act = sha2_256(alloc, { alloc, bstr_type::from_hex(alloc, "") });
                    expect(exp == act) <<fmt::format("{}", act);
                }
                {
                    const value exp { alloc, sha2::hash_256::from_hex("038051e9c324393bd1ca1978dd0952c2aa3742ca4f1bd5cd4611cea83892d382") };
                    const auto act = sha2_256(alloc, { alloc, bstr_type::from_hex(alloc, "de188941a3375d3a8a061e67576e926dc71a7fa3f0cceb97452b4d3227965f9ea8cc75076d9fb9c5417aa5cb30fc22198b34982dbb629e") });
                    expect(exp == act) <<fmt::format("{}", act);
                }
            };
            "sha3_256"_test = [&] {
                {
                    const value exp { alloc, sha3::hash_256::from_hex("a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a") };
                    const auto act = sha3_256(alloc, { alloc, bstr_type::from_hex(alloc, "") });
                    expect(exp == act) <<fmt::format("{}", act);
                }
                {
                    const value exp { alloc, sha3::hash_256::from_hex("33BE80DD552BB39D0AC8212313AE729C26EDE50613491E5ABFB57686ECF037F5") };
                    const auto act = sha3_256(alloc, { alloc, bstr_type::from_hex(alloc, "de188941a3375d3a8a061e67576e926dc71a7fa3f0cceb97452b4d3227965f9ea8cc75076d9fb9c5417aa5cb30fc22198b34982dbb629e") });
                    expect(exp == act) <<fmt::format("{}", act);
                }
            };
            "blake2b_256"_test = [&] {
                {
                    const value exp { alloc, blake2b_256_hash::from_hex("0e5751c026e543b2e8ab2eb06099daa1d1e5df47778f7787faab45cdf12fe3a8") };
                    const auto act = blake2b_256(alloc, { alloc, bstr_type::from_hex(alloc, "") });
                    expect(exp == act) <<fmt::format("{}", act);
                }
                {
                    const value exp { alloc, blake2b_256_hash::from_hex("E1EAE5A8ADAE652EC9AF9677346A9D60ECED61E3A0A69BFACF518DB31F86E36B") };
                    const auto act = blake2b_256(alloc, { alloc, bstr_type::from_hex(alloc, "00010203") });
                    expect(exp == act) <<fmt::format("{}", act);
                }
            };
            "verify_ed25519_signature"_test = [&] {
                static const ed25519::vkey vk {
                        0xe5, 0x2e, 0x09, 0xb2, 0xd3, 0x76, 0x3c, 0x57, 0x00, 0xd5, 0x41, 0xed, 0x9b, 0x88, 0xbe, 0xbd,
                        0xf8, 0x5b, 0x4a, 0x41, 0xd5, 0x42, 0x1a, 0xf1, 0x88, 0x85, 0x46, 0x98, 0x10, 0xf3, 0x17, 0xf7 };
                static const ed25519::signature sig {
                        0xa0, 0xdb, 0x79, 0x88, 0x7b, 0xcb, 0xb9, 0x2e, 0xe9, 0xcf, 0xe9, 0x0e, 0x83, 0x2c, 0x75, 0xab,
                        0xdb, 0xcb, 0xe7, 0x10, 0xb6, 0x29, 0x76, 0x55, 0x35, 0x59, 0x11, 0x33, 0xb4, 0xf2, 0xb6, 0xe6,
                        0xad, 0xfa, 0xb9, 0x33, 0xa8, 0x96, 0xda, 0x75, 0xf2, 0xcd, 0x5d, 0xb3, 0xa3, 0x35, 0x4a, 0x27,
                        0x3d, 0x3e, 0x37, 0xc7, 0x28, 0xca, 0x98, 0x07, 0x53, 0x8d, 0x83, 0x8f, 0xef, 0xbb, 0x2f, 0x00 };
                static const std::array<uint8_t, 84> msg {
                        0xa3, 0x00, 0x81, 0x82, 0x58, 0x20, 0xc1, 0xa1, 0xff, 0x8e, 0x54, 0x99, 0xc3, 0x9f, 0xfa, 0x4c,
                        0x70, 0x67, 0x43, 0x78, 0x5e, 0x62, 0x17, 0xa3, 0x3d, 0xf4, 0x8c, 0xef, 0x73, 0x42, 0xd0, 0xc4,
                        0x52, 0x60, 0x51, 0x58, 0x50, 0xa1, 0x00, 0x01, 0x81, 0x82, 0x58, 0x1d, 0x61, 0xc2, 0x6a, 0xc0,
                        0x99, 0x31, 0xf2, 0xff, 0x67, 0x58, 0x57, 0x30, 0x9b, 0xe6, 0xea, 0xf2, 0xd4, 0xbc, 0x18, 0xd2,
                        0xdd, 0x33, 0xf5, 0x29, 0x0f, 0xc3, 0xa2, 0xad, 0xd1, 0x1a, 0x01, 0x54, 0x45, 0x60, 0x02, 0x1a,
                        0x00, 0x0a, 0xae, 0x60 };
                const value hash { alloc, blake2b<blake2b_256_hash>(std::span<const uint8_t>(msg)) };
                expect(verify_ed25519_signature(alloc, { alloc, vk }, hash, { alloc, sig }).as_bool());
                expect(!verify_ed25519_signature(alloc, { alloc, vk }, { alloc, msg }, { alloc, sig }).as_bool());
            };
            "trace"_test = [&] {
                const value main_term { alloc, 22 };
                expect(trace(alloc, { alloc, "trace msg"sv }, main_term) == main_term);
            };
            "if_then_else"_test = [&] {
                const value val1 { alloc, 12 };
                const value val2 { alloc, "Hello"sv };
                {
                    const auto res = if_then_else(alloc, value::boolean(alloc, true), val1, val2);
                    expect(val1 == res) << fmt::format("{}", res);
                }
                {
                    const auto res = if_then_else(alloc, value::boolean(alloc, false), val1, val2);
                    expect(val2 == res) << fmt::format("{}", res);
                }
                {
                    expect(throws([&] { if_then_else(alloc, { alloc, "avc"sv }, val1, val2); }));
                }
                {
                    expect(throws([&] { if_then_else(alloc, { alloc, 22 }, val1, val2); }));
                }
            };
            "append_string"_test = [&] {
                expect(*append_string(alloc, { alloc, "Hello"sv }, { alloc, " world!"sv }).as_str() == "Hello world!");
                expect(*append_string(alloc, { alloc, ""sv }, { alloc, "AAA"sv }).as_str() == "AAA");
                expect(*append_string(alloc, { alloc, "AAA"sv }, { alloc, ""sv }).as_str() == "AAA");
            };
            "equals_string"_test = [&] {
                expect(equals_string(alloc, { alloc, "hello"sv }, { alloc, "hello"sv }).as_bool());
                expect(!equals_string(alloc, { alloc, "hello"sv }, { alloc, "Hello"sv }).as_bool());
                expect(equals_string(alloc, { alloc, ""sv }, { alloc, ""sv }).as_bool());
                expect(!equals_string(alloc, { alloc, ""sv }, { alloc, "A"sv }).as_bool());
            };
            "encode_utf8"_test = [&] {
                const value s { alloc, "Some UTF8 string: ÖÜ ЯЯ"sv };
                const auto b = encode_utf8(alloc, s);
                expect(b.as_bstr()->str() == *s.as_str());
            };
            "decode_utf8"_test = [&] {
                const value b { alloc, bstr_type { alloc, "Some UTF8 string: ÖÜ ЯЯ"sv } };
                const auto s = decode_utf8(alloc, b);
                expect(b.as_bstr()->str() == *s.as_str());
            };
            "choose_unit"_test = [&] {
                expect(*choose_unit(alloc, value::unit(alloc), { alloc, "AAA"sv }).as_str() == "AAA");
                expect(choose_unit(alloc, value::unit(alloc), { alloc, 22 }).as_int() == 22);
                expect(throws([&] { choose_unit(alloc, value::boolean(alloc, true), value::boolean(alloc, false)); }));
                expect(throws([&] { choose_unit(alloc, value::boolean(alloc, true), value::unit(alloc)); }));
            };
            "fst_pair"_test = [&] {
                expect(fst_pair(alloc, { alloc, constant { alloc, constant_pair { alloc, plutus::constant { alloc, bint_type { alloc, 22 } }, plutus::constant { alloc, bint_type { alloc, 33 } } } } }).as_int() == 22);
                expect(fst_pair(alloc, { alloc, constant { alloc, constant_pair { alloc, plutus::constant { alloc, bint_type { alloc, 33 } }, plutus::constant { alloc, bint_type { alloc, 0 } } } } }).as_int() == 33);
            };
            "snd_pair"_test = [&] {
                expect(snd_pair(alloc, { alloc, constant { alloc, constant_pair(alloc, plutus::constant(alloc, bint_type { alloc, 22 }), plutus::constant(alloc, bint_type { alloc, 33 })) } }).as_int() == 33);
                expect(snd_pair(alloc, { alloc, constant { alloc, constant_pair { alloc, plutus::constant(alloc, bint_type { alloc, 33 }), plutus::constant(alloc, bint_type { alloc, 0 }) } } }).as_int() == 0);
            };
            "choose_list"_test = [&] {
                {
                    expect(choose_list(alloc, value::make_list(alloc, constant_type { alloc, type_tag::integer }), { alloc, bint_type { alloc, 11 } }, { alloc, bint_type { alloc, 22 }}).as_int() == 11);
                }
                {
                    expect(choose_list(alloc, value::make_list(alloc, constant_type { alloc, type_tag::integer }, { constant { alloc, bint_type(alloc, 0) } }),
                        { alloc, bint_type { alloc, 11 } }, { alloc, bint_type { alloc, 22 } }).as_int() == 22);
                }
            };
            "mk_cons"_test = [&] {
                {
                    expect(mk_cons(alloc, { alloc, 22 }, value::make_list(alloc, constant_type { alloc, type_tag::integer })).as_list()->vals.size() == 1_u);
                }
                {
                    constant_list cl { alloc, constant_type { alloc, type_tag::integer }, { { constant { alloc, bint_type(alloc, 0) } } } };
                    const auto res = mk_cons(alloc, value { alloc, 22 }, value { alloc, constant { alloc, std::move(cl) } }).as_list();
                    expect(res->vals.size() == 2_u);
                    expect(res->vals.at(0).as_int() == 22);
                    expect(res->vals.at(1).as_int() == 0);
                }
            };
            "head_list"_test = [&] {
                {
                    expect(throws([&] { head_list(alloc, value::make_list(alloc, constant_type { alloc, type_tag::integer })); }));
                }
                {
                    constant_list::list_type vals { alloc };
                    vals.emplace_back(alloc, bint_type(alloc, 22));
                    constant_list cl { alloc, constant_type { alloc, type_tag::integer }, std::move(vals) };
                    expect(head_list(alloc, value { alloc, constant { alloc, std::move(cl) } }).as_int() == 22);
                }
                {
                    constant_list::list_type vals { alloc };
                    vals.emplace_back(alloc, bint_type(alloc, 22));
                    vals.emplace_back(alloc, bint_type(alloc, 33));
                    vals.emplace_back(alloc, bint_type(alloc, 44));
                    constant_list cl{ alloc, constant_type { alloc, type_tag::integer }, std::move(vals) };
                    expect(head_list(alloc, value { alloc, constant { alloc, std::move(cl) } }).as_int() == 22);
                }
                expect(throws([&] { head_list(alloc, value::make_list(alloc, constant_type { alloc, type_tag::integer })); }));
            };
            "tail_list"_test = [&] {
                {
                    expect(throws([&] { tail_list(alloc, value::make_list(alloc, constant_type { alloc, type_tag::integer })); }));
                }
                {
                    constant_list cl { alloc, constant_type { alloc, type_tag::integer }, { constant { alloc, bint_type(alloc, 22) } } };
                    expect(tail_list(alloc, value { alloc, constant { alloc, std::move(cl) } }).as_list()->vals.empty());
                }
                {
                    constant_list::list_type vals { alloc };
                    vals.emplace_back(alloc, bint_type(alloc, 22));
                    vals.emplace_back(alloc, bint_type(alloc, 33));
                    vals.emplace_back(alloc, bint_type(alloc, 44));
                    constant_list cl{ alloc, constant_type { alloc, type_tag::integer }, std::move(vals) };
                    const auto res = tail_list(alloc, value { alloc, constant { alloc, std::move(cl) } }).as_list();
                    expect(res->vals.size() == 2_u);
                    expect(res->vals.at(0).as_int() == 33);
                    expect(res->vals.at(1).as_int() == 44);
                }
                expect(throws([&] { tail_list(alloc, value::make_list(alloc, constant_type { alloc, type_tag::integer })); }));
            };
            "null_list"_test = [&] {
                {
                    expect(null_list(alloc, value::make_list(alloc, constant_type { alloc, type_tag::integer })).as_bool());
                }
                {
                    constant_list::list_type vals { alloc };
                    vals.emplace_back(alloc, bint_type(alloc, 22));
                    constant_list cl{alloc, constant_type { alloc, type_tag::integer }, std::move(vals) };
                    expect(!null_list(alloc, value { alloc, constant { alloc, std::move(cl) } }).as_bool());
                }
                {
                    constant_list::list_type vals { alloc };
                    vals.emplace_back(alloc, bint_type(alloc, 22));
                    vals.emplace_back(alloc, bint_type(alloc, 33));
                    vals.emplace_back(alloc, bint_type(alloc, 44));
                    constant_list cl {alloc, constant_type { alloc, type_tag::integer }, std::move(vals) };
                    expect(!null_list(alloc, value { alloc, constant { alloc, std::move(cl) } }).as_bool());
                }
            };
            "choose_data"_test = [&] {
                const auto map = map_data(alloc, value::make_list(alloc, constant_type { alloc, type_tag::data }));
                const auto list = list_data(alloc, value::make_list(alloc, constant_type { alloc, type_tag::data }));
                const auto bstr = b_data(alloc, { alloc, bstr_type::from_hex(alloc, "112233") });
                const value r1 { alloc, 1 };
                const value r2 { alloc, 2 };
                const value r3 { alloc, 3 };
                const value r4 { alloc, 4 };
                const value r5 { alloc, 5 };
                expect(choose_data(alloc, constr_data(alloc, { alloc, 5 }, value::make_list(alloc, constant_type { alloc, type_tag::data })), r1, r2, r3, r4, r5) == r1);
                expect(choose_data(alloc, constr_data(alloc, { alloc, 22 }, value::make_list(alloc, constant_type { alloc, type_tag::data })), r1, r2, r3, r4, r5) == r1);
                expect(choose_data(alloc, constr_data(alloc, { alloc, 1000 }, value::make_list(alloc, constant_type { alloc, type_tag::data })), r1, r2, r3, r4, r5) == r1);
                expect(choose_data(alloc, map, r1, r2, r3, r4, r5) == r2);
                expect(choose_data(alloc, list, r1, r2, r3, r4, r5) == r3);
                expect(choose_data(alloc, i_data(alloc, { alloc, 22 }), r1, r2, r3, r4, r5) == r4);
                expect(choose_data(alloc, i_data(alloc, { alloc, -22 }), r1, r2, r3, r4, r5) == r4);
                expect(choose_data(alloc, i_data(alloc, { alloc, cpp_int { 1 } << 80 }), r1, r2, r3, r4, r5) == r4);
                expect(choose_data(alloc, i_data(alloc, { alloc, (cpp_int { 1 } << 80) * -1 }), r1, r2, r3, r4, r5) == r4);
                expect(choose_data(alloc, bstr, r1, r2, r3, r4, r5) == r5);
            };
            "constr_data/un_constr_data"_test = [&] {
                {
                    const value id { alloc, 5 };
                    const auto val = value::make_list(alloc, constant_type { alloc, type_tag::data },
                        { constant { alloc, data::bint(alloc, -5) } });
                    const auto res = constr_data(alloc, id, val);
                    const auto act = un_constr_data(alloc, res).as_pair();
                    test_same(id.as_const(), act.first);
                    test_same(val.as_const(), act.second);
                }
                {
                    const value id { alloc, 25 };
                    const auto val = value::make_list(alloc, constant_type { alloc, type_tag::data },
                        { constant { alloc, data::bint(alloc, 1) }, constant { alloc, data::bint(alloc, 15) } });
                    const auto res = constr_data(alloc, id, val);
                    const auto act = un_constr_data(alloc, res);
                    const auto &act_vals = act.as_pair();
                    expect(act_vals.first == id.as_const());
                    expect(act_vals.second == val.as_const());
                }
                {
                    const value id { alloc, 1025 };
                    const auto val = value::make_list(alloc, constant_type { alloc, type_tag::data },
                        { constant { alloc, data::bstr(alloc, bstr_type::from_hex(alloc, "AABBCC")) },
                            constant { alloc, data::bstr(alloc, bstr_type::from_hex(alloc, "CCBBAA")) } });
                    const auto res = constr_data(alloc, id, val);
                    const auto act = un_constr_data(alloc, res);
                    const auto &act_vals = act.as_pair();
                    expect(act_vals.first == id.as_const());
                    expect(act_vals.second == val.as_const());
                }
            };
            "map_data/un_map_data"_test = [&] {
                {
                    const auto val = value::make_list(alloc, constant_type::make_pair(alloc, constant_type { alloc, type_tag::data }, constant_type { alloc, type_tag::data }));
                    const auto res = map_data(alloc, val);
                    const auto act = un_map_data(alloc, res);
                    expect(val == act) << fmt::format("exp: {} act: {}", val, act);
                }
                {
                    const auto val = value { alloc, constant { alloc, constant_list::make_one(alloc, constant {alloc,
                        constant_pair {alloc,
                            i_data(alloc, { alloc, -5 }).as_const(),
                            i_data(alloc, { alloc, 66 }).as_const()
                        }
                    }) } };
                    const auto res = map_data(alloc, val);
                    const auto act = un_map_data(alloc, res);
                    test_same(val, act);
                }
                {
                    const auto val = value::make_list(alloc, constant_list::list_type { alloc, {
                        constant {alloc,
                            constant_pair(alloc,
                               i_data(alloc, { alloc, -5 }).as_const(),
                               b_data(alloc, { alloc, bstr_type::from_hex(alloc, "112233") }).as_const()
                           )
                        },
                        constant { alloc, constant_pair(alloc,
                            i_data(alloc, { alloc, 17 }).as_const(),
                            b_data(alloc, { alloc, bstr_type::from_hex(alloc, "AABBCC") }).as_const()
                        ) }
                    }});
                    const auto res = map_data(alloc, val);
                    const auto act = un_map_data(alloc, res);
                    expect(val == act) << fmt::format("exp: {} act: {}", val, act);
                }
                expect(throws([&] { map_data(alloc, value::make_list(alloc, { plutus::constant(alloc, bint_type { alloc, 2 }) })); }));
            };
            "list_data/un_list_data"_test = [&] {
                {
                    const auto val = value::make_list(alloc, constant_type { alloc, type_tag::data });
                    const auto res = list_data(alloc, val);
                    const auto act = un_list_data(alloc, res);
                    expect(val == act);
                }
                {
                    const auto val = value::make_list(alloc, {
                        plutus::constant { i_data(alloc, { alloc, bint_type { alloc, -5 } }).as_const() },
                        plutus::constant { i_data(alloc, { alloc, bint_type { alloc, 88 } }).as_const() }
                    });
                    const auto res = list_data(alloc, val);
                    const auto act = un_list_data(alloc, res);
                    expect(val == act) << fmt::format("exp: {} act: {}", val, act);
                }
                {
                    const auto val = value::make_list(alloc, {
                        plutus::constant { b_data(alloc, { alloc, bstr_type::from_hex(alloc, "112233") }).as_const() },
                        plutus::constant { b_data(alloc, { alloc, bstr_type::from_hex(alloc, "556677") }).as_const() }
                    });
                    const auto res = list_data(alloc, val);
                    const auto act = un_list_data(alloc, res);
                    expect(val == act) << fmt::format("exp: {} act: {}", val, act);
                }
                expect(throws([&] { map_data(alloc, value::make_list(alloc, { plutus::constant(alloc, bint_type { alloc, 22 }) } )); }));
            };
            "i_data/un_i_data"_test = [&] {
                {
                    const bint_type val { alloc, -1 };
                    const auto res = i_data(alloc, { alloc, val });
                    test_same(data::bint(alloc, val), res.as_data());
                    test_same(val, un_i_data(alloc, res).as_int());
                }
                {
                    const bint_type val { alloc,std::numeric_limits<uint64_t>::min() };
                    const auto res = i_data(alloc, { alloc, val });
                    test_same(data::bint(alloc, val), res.as_data());
                    test_same(val, un_i_data(alloc, res).as_int());
                }
                {
                    const bint_type val { alloc,0 };
                    const auto res = i_data(alloc, { alloc, val });
                    test_same(data::bint(alloc, val), res.as_data());
                    test_same(val, un_i_data(alloc, res).as_int());
                }
                {
                    const bint_type val { alloc,std::numeric_limits<uint64_t>::max() };
                    const auto res = i_data(alloc, { alloc, val });
                    test_same(data::bint(alloc, val), res.as_data());
                    test_same(val, un_i_data(alloc, res).as_int());
                }
                {
                    const bint_type val { alloc, boost::multiprecision::pow(cpp_int { 2 }, 80) };
                    const auto res = i_data(alloc, { alloc, val });
                    test_same(data::bint(alloc, val), res.as_data());
                    test_same(val, un_i_data(alloc, res).as_int());
                }
            };
            "b_data/un_b_data"_test = [&] {
                {
                    bstr_type::value_type exp { alloc, 65 };
                    const value val { alloc, exp };
                    const auto res = b_data(alloc, val);
                    test_same(data { alloc, bstr_type { alloc, std::move(exp) } }, res.as_data());
                }
                {
                    const auto exp = bstr_type::from_hex(alloc, "001122");
                    const value val { alloc, exp };
                    const auto res = b_data(alloc, val);
                    test_same(data { alloc, exp }, res.as_data());
                }
            };
            "equals_data"_test = [&] {
                expect(equals_data(alloc, { alloc, data::bint(alloc, 123) }, { alloc, data::bint(alloc, 123) }).as_bool());
                expect(!equals_data(alloc, { alloc, data::bint(alloc, 123) }, { alloc, data::bstr(alloc, bstr_type::from_hex(alloc, "1234")) }).as_bool());;
            };
            "mk_pair_data"_test = [&] {
                const bint_type a { alloc, 1 };
                const bint_type b { alloc, 2 };
                const auto p = mk_pair_data(alloc, i_data(alloc, { alloc, a }), i_data(alloc, { alloc, b })).as_pair();
                test_same(data { alloc, a }, p.first.as_data());
                test_same(data { alloc, b }, p.second.as_data());
            };
            "mk_nil_data"_test = [&] {
                expect(mk_nil_data(alloc, value::unit(alloc)) == value::make_list(alloc, constant_type { alloc, type_tag::data }));
            };
            "mk_nil_pair_data"_test = [&] {
                constant_type::list_type nested { alloc };
                nested.emplace_back(alloc, type_tag::data);
                nested.emplace_back(alloc, type_tag::data);
                expect(mk_nil_pair_data(alloc, value::unit(alloc)) == value::make_list(alloc, constant_type { alloc, type_tag::pair, std::move(nested) }));
            };
            "serialize"_test = [&] {
                {
                    const auto val = value::make_list(alloc, {
                        { alloc, constant_pair(alloc,
                            i_data(alloc, { alloc, -5 }).as_const(),
                            b_data(alloc, { alloc, bstr_type::from_hex(alloc, "112233") }).as_const()
                        ) },
                        { alloc, constant_pair(alloc,
                            i_data(alloc, { alloc, 17 }).as_const(),
                            b_data(alloc, { alloc, bstr_type::from_hex(alloc, "AABBCC") }).as_const()
                        ) }
                    });
                    const auto act = serialize_data(alloc, map_data(alloc, val)).as_bstr();
                    const auto exp = bstr_type::from_hex(alloc, "A224431122331143aabbcc");
                    test_same(exp, act);
                }
                {
                    const auto val = value::make_list(alloc, {
                        i_data(alloc, { alloc, -5 }).as_const(),
                        b_data(alloc, { alloc, bstr_type::from_hex(alloc, "112233") }).as_const(),
                        i_data(alloc, { alloc, 17 }).as_const(),
                        b_data(alloc, { alloc, bstr_type::from_hex(alloc, "AABBCC") }).as_const()
                    });
                    const auto act = serialize_data(alloc, list_data(alloc, val)).as_bstr();
                    const auto exp = bstr_type::from_hex(alloc, "9F24431122331143aabbccFF");
                    test_same(exp, act);
                }
                {
                    const auto val = value::make_list(alloc, constant_type { alloc, type_tag::data });
                    const auto act = serialize_data(alloc, list_data(alloc, val)).as_bstr();
                    const auto exp = bstr_type::from_hex(alloc, "80");
                    test_same(exp, act);
                }
            };
        };
        "v3"_test = [] {
            // v3 builtins are tested using the official conformance tests in plutus::machine unit test
        };
    };
};