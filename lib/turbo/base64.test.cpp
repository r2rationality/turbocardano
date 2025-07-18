/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/base64.hpp>
#include <turbo/common/test.hpp>

using namespace turbo;

suite base64_suite = [] {
    "base64"_test = [] {
        "decode_url"_test = [] {
            static std::vector<std::pair<std::string_view, uint8_vector>> test_vectors {
                { "-0Np4pyTOWF26iXWVIvu6fhz9QupwWRS2hcCaOEYlw0", uint8_vector::from_hex("fb4369e29c93396176ea25d6548beee9f873f50ba9c16452da170268e118970d") },
                { "-0Np4pyTOWF26iXWVIvu6fhz9QupwWRS2hcCaOEYlw0=", uint8_vector::from_hex("fb4369e29c93396176ea25d6548beee9f873f50ba9c16452da170268e118970d") },
                { "2pyVSDztfTSLEb8Cur4AjD9_WHhqgm-nY5_robtNnE4", uint8_vector::from_hex("da9c95483ced7d348b11bf02babe008c3f7f58786a826fa7639feba1bb4d9c4e") },
                { "2pyVSDztfTSLEb8Cur4AjD9_WHhqgm-nY5_robtNnE4=", uint8_vector::from_hex("da9c95483ced7d348b11bf02babe008c3f7f58786a826fa7639feba1bb4d9c4e") },
                { "JaoDbxwhRl7B9S1_s41dFgogEszb3zLoIiCCZehmw30", uint8_vector::from_hex("25aa036f1c21465ec1f52d7fb38d5d160a2012ccdbdf32e822208265e866c37d") },
                { "JaoDbxwhRl7B9S1_s41dFgogEszb3zLoIiCCZehmw30=", uint8_vector::from_hex("25aa036f1c21465ec1f52d7fb38d5d160a2012ccdbdf32e822208265e866c37d") }
            };
            for (const auto &[in, exp]: test_vectors) {
                auto out = base64::decode_url(in);
                expect(out.size() == 32) << out.size();
                expect(out == exp) << out;
            }
        };
        "decode"_test = [] {
            static std::vector<std::pair<std::string_view, uint8_vector>> test_vectors {
                { "6MA6A8Cy3b6kGVyvOfQeZp99JR7PIh+7LydcCl1+BdGQ3MJG9WyOM6wANwZuL2ZN2qmF6lKECCZDMI3eT1v+3w==", uint8_vector::from_hex("e8c03a03c0b2ddbea4195caf39f41e669f7d251ecf221fbb2f275c0a5d7e05d190dcc246f56c8e33ac0037066e2f664ddaa985ea5284082643308dde4f5bfedf") },
            };
            for (const auto &[in, exp]: test_vectors) {
                auto out = base64::decode(in);
                expect(out == exp) << out;
            }
        };
    };
};
