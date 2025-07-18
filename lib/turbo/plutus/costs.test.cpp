/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/common/test.hpp>
#include <turbo/plutus/costs.hpp>
#include <turbo/plutus/machine.hpp>

using namespace turbo;
using namespace turbo::plutus;
using namespace turbo::plutus::costs;

suite plutus_costs_suite = [] {
    "plutus::costs"_test = [] {
        "defaults"_test = [&] {
            // The cost functions are tested exhaustively in the plutus::machine unit test where
            // the plutus conformance test is run and the evaluation costs are compared.
            // This file is just a simple test the mimimum API works to not introduce redundancies
            plutus::allocator alloc {};
            const auto &v3 = defaults().v3.value();
            {
                const auto &div = v3.builtin_fun.at(builtin_tag::divide_integer);
                value_list empty { alloc };
                expect_equal(131930, div.cpu->cost(arg_sizes { 1, 1 }, empty));
                expect_equal(1, div.mem->cost(arg_sizes { 1, 1 }, empty));
            }
            const auto &v2 = defaults().v2.value();
            {
                const auto &b = v2.builtin_fun.at(builtin_tag::equals_data);
                const value arg1 { alloc, data::constr(alloc, 0, { data::constr(alloc, 1, { data::bstr(alloc, uint8_vector::from_hex("AABB")) }) }) };
                const value arg2 { alloc, data::constr(alloc, 0, { data::constr(alloc, 1, { data::bstr(alloc, uint8_vector::from_hex("DDDD")) }) }) };
                value_list args { alloc, { arg1, arg2 } };
                const default_size_fun sf {};
                const auto sizes = sf.size(args);
                expect_equal(13, sizes.at(0));
                expect_equal(13, sizes.at(1));
                expect_equal(1252775, b.cpu->cost(sizes, args));
                expect_equal(1, b.mem->cost(sizes, args));
            }
        };
        "model sizes"_test = [] {
            expect_equal(166, cost_arg_names_v1().size());
            expect_equal(175, cost_arg_names_v2().size());
            expect_equal(297, cost_arg_names_v3().size());
        };
    };
};