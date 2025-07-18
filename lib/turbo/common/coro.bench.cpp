/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include "benchmark.hpp"
#include "coro.hpp"

namespace {
    using namespace turbo;
    using namespace turbo::coro;

    generator_t<int> counter(const int max)
    {
        for (int i = 1; i <= max; ++i)
            co_yield i;
    }

    task_t<int> compute()
    {
        co_return 7 * 6;
    }
}

suite turbo_common_coro_bench_suite = [] {
    "turbo::common::coro"_test = [] {
        ankerl::nanobench::Bench b {};
        b.title("turbo::common::coro")
            .output(&std::cerr)
            .unit("create/execute")
            .performanceCounters(true)
            .relative(true);
        b.run("generator_t",[&] {
            auto c = counter(1);
            c.resume();
            ankerl::nanobench::doNotOptimizeAway(c.result());
        });
        b.run("task_t",[&] {
            auto c = compute();
            c.resume();
            ankerl::nanobench::doNotOptimizeAway(c.result());
        });
    };
};