/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/benchmark.hpp>
#include <turbo/zpp.hpp>

using namespace boost::ut;
using namespace turbo;
namespace dt = turbo;

suite zpp_bits_bench_suite = [] {
    "zpp::bits"_test = [] {
        "map of sets"_test = [] {
            using test_item = std::map<std::string, std::set<uint64_t>>;
            test_item items {};
            for (size_t i = 0; i < 1024; ++i) {
                auto &set = items[fmt::format("item{}", i)];
                if (i % 2) {
                    set.emplace(1234);
                    set.emplace(12);
                } else {
                    set.emplace(0);
                }
            }
            uint8_vector data {};
            benchmark("serialize map of sets", [&] {
                auto out = ::zpp::bits::out(data);
                out(items).or_throw();
                ankerl::nanobench::doNotOptimizeAway(items);
            }, data.size());
            test_item out_items {};
            benchmark("deserialize", [&] {
                auto in = ::zpp::bits::in(data);
                in(out_items).or_throw();
                ankerl::nanobench::doNotOptimizeAway(out_items);
            }, data.size());
        };
    };
};
