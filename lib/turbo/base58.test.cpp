/* Copyright (c) 2026 R2 Rationality OÜ. License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */
#include <turbo/base58.hpp>
#include <turbo/common/test.hpp>

using namespace turbo;

suite base58_suite = [] {
    "base58"_test = [] {
        "decode"_test = [] {
            static std::vector<std::pair<const char *, const char *>> test_vectors{
                {"", ""},
                {"111", "000000"},
                {"112", "000001"},
                {"5R", "0100"},
                {"115R", "00000100"},
                {"2g", "61"},
                {"a3gV", "626262"},
                {"StV1DL6CwTryKyV", "68656c6c6f20776f726c64"}
            };
            for (const auto &[in, exp]: test_vectors) {
                expect_equal(base58::decode(in), uint8_vector::from_hex(exp));
            }
            for (const auto text: { "0", "O", "I", "l", " 2", "2\n" })
                expect(throws([&] { base58::decode(text); }));
            for (const auto byte: { 0x00, 0x7F, 0x80, 0xFF }) {
                const char c = static_cast<char>(byte);
                expect(throws([&] { base58::decode(std::string_view { &c, 1 }); }));
            }
        };
    };
};