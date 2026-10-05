/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <string_view>
#include <turbo/cbor/normalize.hpp>
#include <turbo/common/test.hpp>

using namespace turbo;

suite cbor_normalize_suite = [] {
    "cbor::normalize"_test = [] {
        "normalization and idempotence"_test = [] {
            struct sample {
                std::string_view name;
                std::string_view input;
                std::string_view expected;
            };
            const sample samples[] {
                { "empty sequence", "", "" },
                { "empty definite elements", "804060A0", "804060A0" },
                { "empty indefinite elements", "9FFF5FFF7FFFBFFF", "804060A0" },
                { "definite strings with wide lengths", "58036162637803646566", "4361626363646566" },
                { "chunked strings", "5F41614162FF7F61636164FF", "426162626364" },
                { "nested definite containers", "98021800B801180198011802", "8200A1018102" },
                { "indefinite children", "829F1800FFBF18017F61616162FFFF", "828100A101626162" },
                { "definite children", "9F98011800B8011801580161FF", "828100A1014161" },
                { "container map keys", "A19801180098011801", "A181008101" },
                { "map order", "A202000100", "A202000100" },
                { "tagged container", "D9001898011800", "D8188100" },
                { "negative integers", "3800381738183BFFFFFFFFFFFFFFFF", "203738183BFFFFFFFFFFFFFFFF" },
                { "bignum fits", "C24296BE", "1996BE" },
                { "negative bignum fits", "C3435E0F31", "3A005E0F31" },
                { "bignum leading zeros", "C2420000", "00" },
                { "bignum too large", "C249010000000000000000", "C249010000000000000000" },
                { "bignum at uint boundary", "C248FFFFFFFFFFFFFFFF", "1BFFFFFFFFFFFFFFFF" },
                { "bignum at nint boundary", "C348FFFFFFFFFFFFFFFF", "3BFFFFFFFFFFFFFFFF" },
                { "negative bignum too large", "C349010000000000000000", "C349010000000000000000" },
                { "large bignum leading zeros", "C24A00010000000000000000", "C249010000000000000000" },
                { "chunked bignum", "C25F41004196FF", "1896" },
                { "empty bignum magnitudes", "C240C340", "0020" },
                { "nested bignums", "82C24101D9050FC34100", "8201D9050F20" },
                { "float16 vector", "F93C00", "F93C00" },
                { "float16 signed zeros", "F90000F98000", "F90000F98000" },
                { "float16 finite extremes", "F90001F97BFF", "F90001F97BFF" },
                { "float16 infinities", "F97C00F9FC00", "F97C00F9FC00" },
                { "float16 NaN payloads", "F97E01F97D01F9FFFF", "F97E01F97D01F9FFFF" },
                { "nested float16", "9FF93C00BFF93C00D818F98000FFFF", "82F93C00A1F93C00D818F98000" },
                { "simple and float encodings", "F4F5F6FA3F800000", "F4F5F6FA3F800000" }
            };
            for (const auto &s: samples) {
                const auto input = uint8_vector::from_hex(s.input);
                const auto expected = uint8_vector::from_hex(s.expected);
                const auto actual = cbor::normalize(input);
                expect(actual == expected) << s.name << actual;
                expect(cbor::normalize(actual) == actual) << s.name;
            }
        };
        "truncated definite containers"_test = [] {
            for (const auto hex: { "81", "8200", "A1", "A100", "A20001", "818200" }) {
                const auto input = uint8_vector::from_hex(hex);
                expect(throws([&] { static_cast<void>(cbor::normalize(input)); })) << hex;
            }
        };
        "truncated float16"_test = [] {
            for (const auto hex: { "F9", "F93C", "81F93C", "D818F93C" }) {
                const auto input = uint8_vector::from_hex(hex);
                expect(throws([&] { static_cast<void>(cbor::normalize(input)); })) << hex;
            }
        };
        "bignum requires a byte string"_test = [] {
            for (const auto hex: { "C201", "C36161", "C28100" }) {
                const auto input = uint8_vector::from_hex(hex);
                expect(throws([&] { static_cast<void>(cbor::normalize(input)); })) << hex;
            }
        };
    };
};
