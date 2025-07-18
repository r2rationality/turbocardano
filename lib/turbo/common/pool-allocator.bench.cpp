/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include <turbo/common/benchmark.hpp>
#include "pool-allocator.hpp"

namespace {
    using namespace turbo;
}

suite turbo_common_pool_allocator_bench_suite = [] {
    "turbo::common::pool_allocator"_test = [] {
        static constexpr size_t batch_size = 0x400000;
        ankerl::nanobench::Bench b {};
        b.title("turbo::common::pool_allocator")
            .output(&std::cerr)
            .unit("pointer")
            .performanceCounters(true)
            .batch(batch_size)
            .relative(true);
        b.run("new/delete",[&] {
            for (size_t i = 0; i < batch_size; ++i) {
                auto p = new uint64_t;
                ankerl::nanobench::doNotOptimizeAway(p);
                if (i % 2 == 0)
                    delete p;
            }
        });
        b.run("pool_allocator",[&] {
            pool_allocator_t<uint64_t> alloc {};
            for (size_t i = 0; i < batch_size; ++i) {
                auto p = alloc.allocate();
                ankerl::nanobench::doNotOptimizeAway(p);
                if (i % 2 == 0)
                    alloc.deallocate(p);
            }
        });
    };
};