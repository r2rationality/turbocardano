/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/test.hpp>
#include <turbo/parallel/ordered-consumer.hpp>

using namespace turbo;
using namespace turbo::parallel;

suite parallel_ordered_consumer_suite = [] {
    "parallel::ordered_consumer"_test = [] {
        auto &sched = scheduler::get();
        "progress"_test = [&] {
            std::set<uint64_t> processed {};
            ordered_consumer c {
                [&](const auto idx) {
                    processed.emplace(idx);
                },
                "my-consumer", 1000, sched
            };
            expect_equal(false, c.try_push(0));
            expect_equal(true, c.try_push(1000));
            sched.process();
            expect_equal(1000, processed.size());
            expect_equal(false, c.try_push(1000));
            expect_equal(true, c.try_push(1050));
            sched.process();
            expect_equal(1050, processed.size());
            expect_equal(false, c.try_push(1050));
        };
        "one worker at a time"_test = [&] {
            std::set<uint64_t> processed {};
            ordered_consumer c {
                [&](const auto idx) {
                    processed.emplace(idx);
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                },
                "my-consumer", 1000, sched
            };
            expect_equal(false, c.try_push(0));
            expect_equal(true, c.try_push(1));
            expect_equal(false, c.try_push(2));
            expect_equal(false, c.try_push(3));
            sched.process();
            expect_equal(3, processed.size());
            expect_equal(false, c.try_push(3));
            expect_equal(true, c.try_push(4));
            sched.process();
            expect_equal(4, processed.size());
            expect_equal(false, c.try_push(4));
        };
        "parallel pushes"_test = [&] {
            std::set<uint64_t> processed {};
            ordered_consumer c {
                [&](const auto idx) {
                    processed.emplace(idx);
                },
                "my-consumer", 1000, sched
            };
            static constexpr size_t max_idx = 1024;
            if (sched.num_workers() <= 1) [[unlikely]]
                throw error("unit test requires at least two workers!");
            // keep one scheduler worker available for the consumer
            for (size_t i = 0; i < sched.num_workers() - 1; ++i) {
                sched.submit("my-pusher", 100, [&] {
                    for (;;) {
                        const auto next_idx = c.next();
                        if (next_idx >= max_idx)
                            break;
                        c.try_push(next_idx + 1);
                    }
                });
            }
            sched.process();
            expect_equal(max_idx, processed.size());
        };
        "failing consumer"_test = [&] {
            std::set<uint64_t> processed {};
            ordered_consumer c {
                [&](const auto idx) {
                    if (idx == 10)
                        throw error("unsupported index!");
                    processed.emplace(idx);
                },
                "my-consumer", 1000, sched
            };
            expect_equal(false, c.try_push(0));
            expect_equal(true, c.try_push(1));
            sched.process();
            expect_equal(true, c.try_push(2));
            sched.process();
            expect_equal(true, c.try_push(10));
            sched.process();
            expect_equal(10, processed.size());
            expect_equal(false, c.try_push(10));
            expect_equal(true, c.try_push(11));
            sched.process();
            expect_equal(10, processed.size());
            expect(throws([&] { c.try_push(11); }));
            expect_equal(10, c.next());
        };
    };
};
