/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include "benchmark.hpp"
#include "numeric-cast.hpp"

namespace {
    using namespace turbo;
}

suite turbo_common_numeric_cast_bench_suite = [] {
    "turbo::common::numeric_cast"_test = [] {
        ankerl::nanobench::Bench b {};
        b.title("turbo::common::numeric_cast")
            .output(&std::cerr)
            .unit("cast")
            .performanceCounters(true)
            .relative(true)
            .batch(3);
        b.run("static_cast",[&] {
            const auto a = static_cast<uint8_t>(uint64_t{0});
            const auto b = static_cast<uint8_t>(uint64_t{24});
            const auto c = static_cast<uint8_t>(uint64_t{255});
            ankerl::nanobench::doNotOptimizeAway(a + b + c);
        });
        b.run("both unsigned",[&] {
            const auto a = numeric_cast<uint8_t>(uint64_t{0});
            const auto b = numeric_cast<uint8_t>(uint64_t{24});
            const auto c = numeric_cast<uint8_t>(uint64_t{255});
            ankerl::nanobench::doNotOptimizeAway(a + b + c);
        });
        b.run("both signed",[&] {
            const auto a = numeric_cast<int8_t>(int64_t{-128});
            const auto b = numeric_cast<int8_t>(int64_t{24});
            const auto c = numeric_cast<uint8_t>(uint64_t{127});
            ankerl::nanobench::doNotOptimizeAway(a + b + c);
        });
        b.run("signed to unsigned",[&] {
            const auto a = numeric_cast<uint8_t>(int64_t{0});
            const auto b = numeric_cast<uint8_t>(int64_t{24});
            const auto c = numeric_cast<uint8_t>(int64_t{255});
            ankerl::nanobench::doNotOptimizeAway(a + b + c);
        });
        b.run("unsigned to signed",[&] {
            const auto a = numeric_cast<int8_t>(uint64_t{0});
            const auto b = numeric_cast<int8_t>(uint64_t{24});
            const auto c = numeric_cast<int8_t>(uint64_t{127});
            ankerl::nanobench::doNotOptimizeAway(a + b + c);
        });
    };
};