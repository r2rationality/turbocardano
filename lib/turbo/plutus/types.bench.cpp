/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/common/benchmark.hpp>
#include <turbo/common/memory.hpp>
#include <turbo/plutus/types.hpp>

namespace {
    using namespace turbo;
    using namespace turbo::plutus;

    template<typename T, typename ...Args>
    void bench_simple(Args... args)
    {
        const auto mem_before = memory::my_usage_mb();
        {
            plutus::allocator alloc {};
            static constexpr size_t num_objs = 1 << 20;
            benchmark(typeid(T).name(), [&] {
                for (size_t i = 0; i < num_objs; ++i)
                    alloc.make<T>(std::forward<Args>(args)...);
            }, num_objs);
        }
        expect_equal(mem_before, memory::my_usage_mb());
    }

    template<typename T, typename ...Args>
    void bench_nested(Args... args)
    {
        const auto mem_before = memory::my_usage_mb();
        {
            plutus::allocator alloc {};
            static constexpr size_t num_objs = 1 << 20;
            benchmark(typeid(T).name(), [&] {
                for (size_t i = 0; i < num_objs; ++i)
                    alloc.make<T>(alloc, std::forward<Args>(args)...);
            }, num_objs);
        }
        expect_equal(mem_before, memory::my_usage_mb(), typeid(T).name());
    }
}

suite plutus_types_suite = [] {
    "plutus::types"_test = [] {
        using namespace std::string_view_literals;
        plutus::allocator base {};
        plutus::str_type s1 { base, "some string"sv };
        auto b1 = bstr_type::from_hex(base, "DEADBEAF");
        auto i1 = bint_type { base, 123 };
        plutus::constant c1 { base, false };
        plutus::constant c2{ base, s1 };
        term t1 { base, c1 };
        term t2 { base, c2 };
        value v1 { base, c1 };
        bench_nested<value>(c1);
        bench_nested<value_list>(std::initializer_list<value> { v1, v1 });
        bench_nested<term>(c1);
        bench_nested<term_list>(std::initializer_list<term> { t1, t2 });
        bench_simple<variable>(size_t { 0 });
        bench_simple<force>(t1);
        bench_simple<plutus::apply>(t1, t2);
        bench_simple<failure>();
        bench_simple<t_delay>(t1);
        bench_simple<t_lambda>(size_t { 0 }, t1);
        bench_nested<plutus::data>(i1);
        auto db1 = data::bstr(base, *b1);
        auto di1 = data::bint(base, i1);
        data::map_type dm1 { base, { data_pair { base, db1, di1 } } };
        bench_nested<plutus::data>(i1);
        bench_nested<plutus::data>(b1);
        bench_nested<plutus::data>(dm1);
        bench_nested<plutus::constant>(false);
        bench_nested<plutus::constant>(s1);
        bench_nested<plutus::constant>(b1);
        bench_nested<plutus::constant>(i1);
        bench_nested<plutus::constant_pair>(c1, c1);
        bench_nested<plutus::constant_type>(type_tag::integer);
        constant_type ct1 { base, type_tag::integer };
        bench_nested<plutus::constant_list>(ct1, std::initializer_list<plutus::constant> { c1, c2 });
    };
};