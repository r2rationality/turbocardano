/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/common/scheduler.hpp>
#include <turbo/common/test.hpp>
#include <turbo/parallel/ordered-queue.hpp>

using namespace turbo;
using namespace turbo::parallel;

suite parallel_ordered_queue_suite = [] {
    using boost::ext::ut::v2_1_0::nothrow;
    "parallel::ordered_queue"_test = [] {
        "out of order put"_test = [] {
            ordered_queue q {};
            q.put(3);
            expect_equal(false, !!q.take());
            q.put(1);
            expect_equal(false, !!q.take());
            q.put(2);
            expect_equal(false, !!q.take());
            q.put(0);
            expect_equal(ordered_queue::optional_index { 0 }, q.take());
            expect_equal(ordered_queue::optional_index { 1 }, q.take());
            expect_equal(ordered_queue::optional_index { 2 }, q.take());
            expect_equal(ordered_queue::optional_index { 3 }, q.take());
            expect_equal(false, !!q.take());
        };
        "index_too_big"_test = [] {
            ordered_queue q {};
            expect(nothrow([&]{ q.put(65536); }));
            expect_equal(false, !!q.take());
            expect(nothrow([&]{ q.put(std::numeric_limits<uint64_t>::max() - 1); }));
            expect_equal(false, !!q.take());
            expect(throws([&]{ q.put(std::numeric_limits<uint64_t>::max()); }));
            expect_equal(false, !!q.take());
        };
        "parallel put and take ordered queue"_test = [] {
            static constexpr size_t items_per_worker = 1024;
            auto &sched = scheduler::get();
            ordered_queue q {};
            for (size_t i = 0; i < sched.num_workers(); ++i) {
                sched.submit("parallel-put", 100, [&, i] {
                    size_t idx = i;
                    for (size_t j = 0; j < items_per_worker; ++j) {
                        q.put(idx);
                        idx += sched.num_workers();
                    }
                });
            }
            sched.process();
            const auto total_items = items_per_worker * sched.num_workers();
            std::atomic_size_t ok { 0 };
            std::atomic_size_t err { 0 };
            for (size_t i = 0; i < sched.num_workers(); ++i) {
                sched.submit("parallel-take", 100, [&] {
                    for (size_t j = 0; j < items_per_worker; ++j) {
                        const auto v = q.take();
                        if (v)
                            ok.fetch_add(1, std::memory_order_relaxed);
                        else
                            err.fetch_add(1, std::memory_order_relaxed);
                    }
                });
            }
            sched.process();
            expect_equal(total_items, ok.load(std::memory_order_relaxed));
            expect_equal(0, err.load(std::memory_order_relaxed));
            expect_equal(false, !!q.take());
        };
    };
};