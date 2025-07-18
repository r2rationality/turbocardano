/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include "benchmark.hpp"

namespace {
    using namespace turbo;
}

suite turbo_common_bytes_bench_suite = [] {
    "turbo::common::bytes"_test = [] {
        ankerl::nanobench::Bench b {};
        static const std::string_view hex_input { "00112233445566778899AABBCCDDEEFF00112233445566778899AABBCCDDEEFF" };
        b.title("turbo::common::bytes")
            .output(&std::cerr)
            .unit("char")
            .performanceCounters(true)
            .relative(true)
            .batch(hex_input.size());
        b.run("from_hex",[&] {
            byte_array<32> res;
            init_from_hex(res, hex_input);
            ankerl::nanobench::doNotOptimizeAway(res);
        });
    };
};