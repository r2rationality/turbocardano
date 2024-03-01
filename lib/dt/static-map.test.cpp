/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2024 Alex Sierkov (alex dot sierkov at gmail dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/static-map.hpp>
#include <dt/test.hpp>

using namespace daedalus_turbo;

suite static_map_suite = [] {
    "static_map"_test = [] {
        std::map<uint64_t, uint64_t> src {};
        src.emplace(0, 5);
        src.emplace(22, 4);
        src.emplace(21, 7);
        "copy construct"_test = [&] {
            static_map<uint64_t, uint64_t> dst {};
            dst = src;
            expect(dst.size() == src.size()) << dst.size();
        };
        "contains construct"_test = [&] {
            static_map<uint64_t, uint64_t> dst {};
            dst = src;
            expect(dst.contains(0));
            expect(!dst.contains(1));
            expect(dst.contains(22));
            expect(!dst.contains(23));
            expect(dst.contains(21));
        };
        "get construct"_test = [&] {
            static_map<uint64_t, uint64_t> dst {};
            dst = src;
            expect(dst.get(0) == 5_ull);
            expect(dst.get(22) == 4_ull);
            expect(dst.get(21) == 7_ull);
            expect(dst.get(1) == 0_ull);
            expect(dst.get(23) == 0_ull);
        };
        "find construct"_test = [&] {
            static_map<uint64_t, uint64_t> dst {};
            dst = src;
            {
                auto it = dst.find(0);
                expect(it != dst.end());
                if (it != dst.end())
                    expect(it->second == 5_ull);
            }
            {
                auto it = dst.find(22);
                expect(it != dst.end());
                if (it != dst.end())
                    expect(it->second == 4_ull);
            }
            {
                auto it = dst.find(21);
                expect(it != dst.end());
                if (it != dst.end())
                    expect(it->second == 7_ull);
            }
            expect(dst.find(1) == dst.end());
            expect(dst.find(23) == dst.end());
        };
    };
};