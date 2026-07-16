/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/benchmark.hpp>
#include <turbo/common/scheduler.hpp>

using namespace turbo;

suite atomic_bench_suite = [] {
    "atomic"_test = [] {
        auto &sched = scheduler::get();
        std::atomic<size_t> counter { 0 };
        const std::string task { "increment" };
        static constexpr size_t max_count = 10e7;
        const auto num_workers = std::min(size_t { 1 }, sched.num_workers());
        benchmark("memory_order_relaxed", [&]() {
            sched.wait_all(task, [&](const auto &, const auto &submit_f) {
                for (size_t i = 0; i < num_workers; ++i) {
                    submit_f({100, task, [&] {
                        while (counter.fetch_add(1, std::memory_order_relaxed) < max_count) {
                            // just repeat
                        }
                    }});
                }
            });
        }, max_count);
        counter.store(0, std::memory_order_relaxed);
        benchmark("memory_order_release", [&]() {
            sched.wait_all(task, [&](const auto &, const auto &submit_f) {
                for (size_t i = 0; i < num_workers; ++i) {
                    submit_f({100, task, [&] {
                        while (counter.fetch_add(1, std::memory_order_release) < max_count) {
                            // just repeat
                        }
                    }});
                }
            });
        }, max_count);
        counter.store(0, std::memory_order_relaxed);
        benchmark("memory_order_acq_rel", [&]() {
            sched.wait_all(task, [&](const auto &, const auto &submit_f) {
                for (size_t i = 0; i < num_workers; ++i) {
                    submit_f({ 100, task, [&] {
                        while (counter.fetch_add(1, std::memory_order_acq_rel) < max_count) {
                            // just repeat
                        }
                    }});
                }
            });
        }, max_count);
        counter.store(0, std::memory_order_relaxed);
        benchmark("memory_order_seq_cst", [&]() {
            sched.wait_all(task, [&](const auto &, const auto &submit_f) {
                for (size_t i = 0; i < num_workers; ++i) {
                    submit_f({ 100, task, [&] {
                        while (counter.fetch_add(1, std::memory_order_seq_cst) < max_count) {
                            // just repeat
                        }
                    }});
                }
            });
        }, max_count);
    };
};