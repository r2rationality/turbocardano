/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include <turbo/common/coro.hpp>
#include <turbo/common/test.hpp>

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

    task_t<std::string> greet()
    {
        co_return "hello, coroutine!";
    }

    task_t<int> fail()
    {
        throw std::runtime_error("error in coroutine");
        co_return 0; // unreachable but need to indicate a coroutine to compilers
    }
}

suite turbo_common_coro_suite = [] {
    "turbo::common::coro"_test = [] {
        "generator_t multiple co_yield"_test = [] {
            std::vector<int> v {};
            auto gen = counter(2);
            while (gen.resume()) {
                v.emplace_back(gen.result());
            }
            expect_equal(std::vector<int> { 1, 2 }, v);
        };

        "generator_t yields values in order"_test = [] {
            auto c = counter(3);

            expect_equal(true, c.resume());
            expect_equal(1, c.result());

            expect_equal(true, c.resume());
            expect_equal(2, c.result());

            expect_equal(true, c.resume());
            expect_equal(3, c.result());

            expect_equal(false, c.resume()); // No more values
        };

        "generator_t throws on empty take"_test = [] {
            auto c = counter(1);

            expect_equal(true, c.resume());
            expect_equal(1, c.result());

            // Now coroutine is done, take() without value should throw
            expect(throws([&]{ c.result(); }));
        };

        "task_t returns correct result"_test = [] {
            auto c = compute();
            c.resume();
            expect_equal(42, c.result());
        };

        "task_t works with std::string"_test = [] {
            auto c = greet();
            c.resume();
            expect_equal("hello, coroutine!", c.result());
        };

        "task_t propagates exception"_test = [] {
            auto c = fail();
            c.resume();
            expect(throws<std::runtime_error>([&]{ c.result(); }));
        };

        "task_t is movable"_test = [] {
            auto c1 = compute();
            task_t<int> c2 = std::move(c1);
            c2.resume();
            expect_equal(42, c2.result());
        };

        "external_task_t"_test = [] {
            size_t coro_steps = 0;
            size_t num_resumes = 0;
            std::coroutine_handle<> active_handle {};

            auto my_coro = [&] -> task_t<void> {
                ++coro_steps;
                co_await external_task_t{[&](auto h) { active_handle = h; }};
                ++coro_steps;
                co_await external_task_t{[&](auto h) { active_handle = h; }};
                ++coro_steps;
                co_await external_task_t{[&](auto h) { active_handle = h; }};
                ++coro_steps;
                co_await external_task_t{[&](auto h) { active_handle = h; }};
                ++coro_steps;
                co_return;
            };

            auto c1 = my_coro();
            ++num_resumes;
            c1.resume();

            for (size_t i = 0; i < 4; ++i) {
                expect(!!active_handle);
                if (active_handle) {
                    ++num_resumes;
                    active_handle.resume();
                }
            }

            expect(c1.done());
            expect_equal(5ULL, coro_steps);
            expect_equal(5ULL, num_resumes);
        };

        "nested tasks"_test = [] {
            auto coro_1 = [] -> task_t<int> {
                co_return 1;
            };

            auto coro_2 = [&] -> task_t<int> {
                const auto c1_res = co_await coro_1();
                co_return c1_res + 1;
            };

            auto my_coro = coro_2();
            my_coro.resume();
            scheduler::get().process();
            expect(my_coro.done());
            const auto res = my_coro.result();
            expect_equal(2, res);
        };
    };
};