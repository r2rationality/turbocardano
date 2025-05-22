/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/common/benchmark.hpp>
#include <dt/scheduler.hpp>

using namespace daedalus_turbo;

suite atomic_bench_suite = [] {
    "atomic"_test = [] {
        auto &sched = scheduler::get();
        std::atomic<size_t> counter { 0 };
        const std::string task { "increment" };
        static constexpr size_t max_count = 10e7;
        const auto num_workers = std::min(size_t { 1 }, sched.num_workers());
        const auto relaxed = benchmark_rate("memory_order_relaxed", 3, [&]() {
            sched.wait_all_done(task, num_workers, [&] {
                for (size_t i = 0; i < num_workers; ++i) {
                    sched.submit_void(task, 100, [&] {
                        while (counter.fetch_add(1, std::memory_order_relaxed) < max_count) {
                            // just repeat
                        }
                    });
                }
            });
            return max_count;
        });
        counter.store(0, std::memory_order_relaxed);
        const auto release = benchmark_rate("memory_order_release", 3, [&]() {
            sched.wait_all_done(task, num_workers, [&] {
                for (size_t i = 0; i < num_workers; ++i) {
                    sched.submit_void(task, 100, [&] {
                        while (counter.fetch_add(1, std::memory_order_release) < max_count) {
                            // just repeat
                        }
                    });
                }
            });
            return max_count;
        });
        counter.store(0, std::memory_order_relaxed);
        const auto acq_rel = benchmark_rate("memory_order_acq_rel", 3, [&]() {
            sched.wait_all_done(task, num_workers, [&] {
                for (size_t i = 0; i < num_workers; ++i) {
                    sched.submit_void(task, 100, [&] {
                        while (counter.fetch_add(1, std::memory_order_acq_rel) < max_count) {
                            // just repeat
                        }
                    });
                }
            });
            return max_count;
        });
        counter.store(0, std::memory_order_relaxed);
        const auto seq_cst = benchmark_rate("memory_order_seq_cst", 3, [&]() {
            sched.wait_all_done(task, num_workers, [&] {
                for (size_t i = 0; i < num_workers; ++i) {
                    sched.submit_void(task, 100, [&] {
                        while (counter.fetch_add(1, std::memory_order_seq_cst) < max_count) {
                            // just repeat
                        }
                    });
                }
            });
            return max_count;

        expect(relaxed > release * 0.95);});
        expect(relaxed > seq_cst * 0.95);
        expect(relaxed > acq_rel * 0.95);
    };
};