#pragma once
/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include <nanobench.h>
#include "test.hpp"

namespace turbo {
    void benchmark(const std::string &name, const auto &action, const size_t batch_size=1)
    {
        ankerl::nanobench::Bench b {};
        b.title(name)
            .output(&std::cerr)
            .unit("item")
            .performanceCounters(true)
            .relative(true)
            .batch(batch_size);
        b.run("benchmark",[&] {
            ankerl::nanobench::doNotOptimizeAway(action);
        });
    }
}