/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/bytes.hpp>
#include <turbo/common/test.hpp>
#include <turbo/util.hpp>

using namespace std::literals;
using namespace turbo;

suite util_suite = [] {
    "util"_test = [] {
        "binary_search"_test = [] {
            std::array<int, 8> data { 0, 10, 20, 30, 40, 50, 60, 70 };
            std::vector<std::pair<int, bool>> test_vector {
                { -1, false }, { 0, true }, { 5, false }, { 30, true },
                { 50, true }, { 65, false }, { 70, true }, { 75, false }
            };
            for (const auto &[val, exp]: test_vector) {
                auto it = turbo::binary_search(data.begin(), data.end(), val, [&](const auto &el, const auto &val) { return el < val; });
                expect((it != data.end()) == exp);
                if (it != data.end()) expect(*it == val);
            }
        };

        "net_to_host"_test = [] {
            static int x = 1;
            uint64_t val = 0x00000000DEADBEAF;
            expect(net_to_host(host_to_net(val)) == val);
            if (*reinterpret_cast<char *>(&x) == 1) {
                expect(host_to_net(val) == 0xAFBEADDE00000000);
                expect(net_to_host(0xAFBEADDE00000000) == val);
            }
        };

        "buffer_readable"_test = [] {
            {
                const auto bytes = uint8_vector::from_hex("2389d40886678db816de5ff85d81c12aaec51b11dcb0187aed73a815838cc5a7");
                const auto text = fmt::format("{}", buffer_readable { bytes });
                expect_equal(std::string_view { "2389D40886678DB816DE5FF85D81C12AAEC51B11DCB0187AED73A815838CC5A7" }, text);
            }
            {
                const auto bytes = uint8_vector::from_hex("534f554c");
                const auto text = fmt::format("{}", buffer_readable { bytes });
                expect_equal(std::string_view { "'SOUL'" }, text);
            }
        };
        "buffer comparisons"_test = [] {
            const auto d1 = uint8_vector::from_hex("001122");
            const auto d2 = uint8_vector::from_hex("001122");
            const auto d3 = uint8_vector::from_hex("001133");
            expect_equal(d1, d2);
            expect(!(d1 < d2));
            expect(!(d2 < d1));
            expect(d1 != d3);
            expect(d1 < d3);
            expect(!(d3 < d1));
        };
    };
};