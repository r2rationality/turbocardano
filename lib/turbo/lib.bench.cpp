/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <functional>
#include <turbo/common/benchmark.hpp>

namespace {
    using namespace boost::ut;
    using namespace turbo;
    namespace dt = turbo;

#ifndef __APPLE__
    using move_func = std::move_only_function<uint64_t(uint64_t)>;
    static_assert(sizeof(move_func) <= 64);

    uint64_t f3_move(move_func f)
    {
        static uint64_t ctr = 0;
        return f(ctr++);
    }

    uint64_t f2_move(move_func f)
    {
        return f3_move(std::move(f));
    }

    uint64_t f1_move(move_func f)
    {
        return f2_move(std::move(f));
    }
#endif

    using copy_func = std::function<uint64_t(uint64_t)>;
    static_assert(sizeof(copy_func) <= 64);

    uint64_t f3_copy(const copy_func &f)
    {
        static uint64_t ctr = 0;
        return f(ctr++);
    }

    uint64_t f2_copy(const copy_func &f)
    {
        return f3_copy(f);
    }

    uint64_t f1_copy(const copy_func &f)
    {
        return f2_copy(f);
    }

    using ptr_func = uint64_t(*)(uint64_t);
    static_assert(sizeof(ptr_func) == 8);

    uint64_t f3_ptr(const ptr_func f)
    {
        static uint64_t ctr = 0;
        return f(ctr++);
    }

    uint64_t f2_ptr(const ptr_func f)
    {
        return f3_ptr(f);
    }

    uint64_t f1_ptr(const ptr_func f)
    {
        return f2_ptr(f);
    }

    struct nested_struct_3 {
        uint64_t a;
        uint64_t b;

        static nested_struct_3 make(const uint64_t x)
        {
            return { x * 55, x +23 };
        }
    };

    struct nested_struct_2 {
        nested_struct_3 x;
        nested_struct_3 y;

        static nested_struct_2 make(const uint64_t i)
        {
            return { decltype(x)::make(i + 1), decltype(y)::make(i + 2) };
        }
    };

    struct nested_struct_1 {
        nested_struct_2 c;
        nested_struct_2 d;

        static nested_struct_1 make(const uint64_t i)
        {
            return { decltype(c)::make(2 * i), decltype(d)::make(3 * i) };
        }
    };

    struct nested_3 {
        uint64_t a;
        uint64_t b;

        nested_3(const uint64_t x):
            a { x * 55 },
            b { x + 23}
        {
        }
    };

    struct nested_2 {
        nested_3 x;
        nested_3 y;

        nested_2(const uint64_t i):
            x { i + 1 },
            y { i + 2 }
        {
        }
    };

    struct nested_1 {
        nested_2 c;
        nested_2 d;

        nested_1(const uint64_t i):
            c { 2 * i },
            d { 3 * i }
        {
        }
    };
}

suite lib_primitives_bench_suite = [] {
    "lib::primitives"_test = [] {
        {
            ankerl::nanobench::Bench b {};
            b.title("function as an argument")
                .output(&std::cerr)
                .performanceCounters(true)
                .relative(true);
            {
                b.run("std::function",[&] {
                    ankerl::nanobench::doNotOptimizeAway(f1_copy([](uint64_t x) { return x << 4; }));
                });
#ifndef __APPLE__
                b.run("std::move_only_function", [&] {
                    ankerl::nanobench::doNotOptimizeAway(f1_move([](uint64_t x) { return x << 4; }));
                });
#endif
                b.run("function pointer", [&] {
                    ankerl::nanobench::doNotOptimizeAway(f1_ptr([](uint64_t x) { return x << 4; }));
                });
            }
        }
        {
            ankerl::nanobench::Bench b {};
            b.title("instance construction")
                .output(&std::cerr)
                .performanceCounters(true)
                .relative(true);
            {
                b.run("constructor",[&] {
                    ankerl::nanobench::doNotOptimizeAway(nested_1 { 15 });
                    ankerl::nanobench::doNotOptimizeAway(nested_1 { 30 });
                });
                b.run("factory method", [&] {
                    ankerl::nanobench::doNotOptimizeAway(nested_struct_1::make(15));
                    ankerl::nanobench::doNotOptimizeAway(nested_struct_1::make(30));
                });
            }
        }
    };
};
