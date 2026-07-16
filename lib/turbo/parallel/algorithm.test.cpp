/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/scheduler.hpp>
#include <turbo/common/test.hpp>
#include <turbo/parallel/algorithm.hpp>

namespace {
    using namespace turbo;
    using namespace turbo::parallel;
}

suite turbo_parallel_algorithm_suite = [] {
    using boost::ext::ut::v2_1_0::nothrow;
    "turbo::parallel::algorithm"_test = [] {
        "atomic_max sequential"_test = [] {
            std::atomic<size_t> val { 10U };
            atomic_max(val, size_t { 5U });
            expect_equal(10, val.load(std::memory_order_relaxed));
            atomic_max(val, size_t { 10U });
            expect_equal(10, val.load(std::memory_order_relaxed));
            atomic_max(val, size_t { 20U });
            expect_equal(20, val.load(std::memory_order_relaxed));
        };
        "atomic_max parallel"_test = [] {
            std::atomic<size_t> val { 10U };
            auto &sched = scheduler::get();
            static constexpr size_t max_val = 1ULL << 24U;
            const auto num_workers = sched.num_workers();
            for (size_t i = 0; i < num_workers; ++i) {
                sched.submit("atomic-max", 100, [&, i] {
                    for (size_t j = i; j < max_val; j += num_workers) {
                        atomic_max(val, j);
                    }
                });
            }
            sched.process();
            expect_equal(max_val - 1, val.load(std::memory_order_relaxed));
        };
    };
};