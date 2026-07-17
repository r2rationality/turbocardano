/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/test.hpp>
#include <turbo/plutus/costs.hpp>
#include <turbo/plutus/machine.hpp>

using namespace turbo;
using namespace turbo::plutus;
using namespace turbo::plutus::costs;

namespace {
    void expect_arg_sizes(const std::initializer_list<uint64_t> expected, const arg_sizes &actual,
        const std::source_location &loc=std::source_location::current())
    {
        if (!expect_equal(expected.size(), actual.size(), loc))
            return;
        size_t idx = 0;
        for (const auto size: expected)
            expect_equal(size, actual.at(idx++), loc);
    }
}

suite plutus_costs_suite = [] {
    "plutus::costs"_test = [] {
        "defaults"_test = [&] {
            // The cost functions are tested exhaustively in the plutus::machine unit test where
            // the plutus conformance test is run and the evaluation costs are compared.
            // This file is just a simple test the mimimum API works to not introduce redundancies
            plutus::allocator alloc {};
            const auto &v3 = defaults().for_script(cardano::script_type::plutus_v3, builtin_semantics::c);
            {
                const auto &div = v3.builtin_fun.at(builtin_tag::divide_integer);
                value_list empty { alloc };
                expect_equal(131930, div.cpu->cost(arg_sizes { 1, 1 }, empty));
                expect_equal(85848, div.cpu->cost(arg_sizes { 100, 100 }, empty));
                expect_equal(1, div.mem->cost(arg_sizes { 1, 1 }, empty));
            }
            const auto &v2 = defaults().for_script(cardano::script_type::plutus_v2, builtin_semantics::b);
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
        "string sizes follow semantics variant"_test = [] {
            allocator alloc {};
            value_list args { alloc, {
                value { alloc, std::string_view { "\xC3\xA9\xC3\xA9" } }
            } };
            const auto &op = defaults().for_script(cardano::script_type::plutus_v3, builtin_semantics::c).builtin_fun.at(builtin_tag::encode_utf8);
            expect_arg_sizes({ 2 }, sizes_for(op, builtin_tag::encode_utf8, args, false));
            expect_arg_sizes({ 1 }, sizes_for(op, builtin_tag::encode_utf8, args, true));
        };
        "argument sizes are demand-driven"_test = [] {
            expect_equal(2, arg_sizes { 1, 2 }.size());

            allocator alloc {};
            value_list empty { alloc };
            size_t calls = 0;
            arg_sizes sizes { 2, [&calls](const size_t idx) {
                ++calls;
                return idx + 1;
            } };

            const auto &model = defaults().for_script(cardano::script_type::plutus_v1, builtin_semantics::a);
            const auto &constant = model.builtin_fun.at(builtin_tag::choose_unit);
            constant.cpu->cost(sizes, empty);
            constant.mem->cost(sizes, empty);
            expect_equal(0, calls);

            const auto &uses_both = model.builtin_fun.at(builtin_tag::append_byte_string);
            uses_both.cpu->cost(sizes, empty);
            uses_both.mem->cost(sizes, empty);
            expect_equal(2, calls);
        };
        "semantics variant models"_test = [] {
            cardano::plutus_cost_models no_overrides {};
            const auto models = parse(no_overrides);
            expect(throws([&] { models.for_script(cardano::script_type::plutus_v3, builtin_semantics::a); }));

            allocator alloc {};
            value_list empty { alloc };
            const auto &d_div = models.for_script(cardano::script_type::plutus_v1, builtin_semantics::d)
                .builtin_fun.at(builtin_tag::divide_integer);
            expect_equal(d_div.cpu->cost(arg_sizes { 1, 100 }, empty),
                d_div.cpu->cost(arg_sizes { 100, 1 }, empty));

            const auto &exp_mod = models.for_script(cardano::script_type::plutus_v3, builtin_semantics::e)
                .builtin_fun.at(builtin_tag::exp_mod_integer);
            expect_equal(2953927, exp_mod.cpu->cost(arg_sizes { 1, 2, 3 }, empty));
            expect_equal(4430890, exp_mod.cpu->cost(arg_sizes { 4, 2, 3 }, empty));
            expect_equal(3, exp_mod.mem->cost(arg_sizes { 1, 2, 3 }, empty));
        };
        "cost sizing and saturation"_test = [] {
            allocator alloc {};
            const value_list negative_word_boundary { alloc, {
                value { alloc, (cpp_int { 1 } << 64) * -1 }
            } };
            expect_arg_sizes({ 2 }, default_size_fun {}.size(negative_word_boundary));

            cardano::plutus_cost_models no_overrides {};
            const auto models = parse(no_overrides);
            const auto &replicate = models.for_script(cardano::script_type::plutus_v1, builtin_semantics::d)
                .builtin_fun.at(builtin_tag::replicate_byte);
            const value_list negative_byte_count { alloc, {
                value { alloc, cpp_int { -9 } }, value { alloc, cpp_int { 0 } }
            } };
            expect_arg_sizes({ 2, 0 }, replicate.size->size(negative_byte_count));

            const literal_in_x_size_fun literal_sizer {};
            const value_list positive_literal { alloc, {
                value { alloc, cpp_int { 7 } }, value { alloc, cpp_int { 0 } }
            } };
            const value_list negative_literal { alloc, {
                value { alloc, cpp_int { -7 } }, value { alloc, cpp_int { 0 } }
            } };
            const value_list saturated_literal { alloc, {
                value { alloc, cpp_int { -1 } << 100 }, value { alloc, cpp_int { 0 } }
            } };
            expect_arg_sizes({ 7, 1 }, literal_sizer.size(positive_literal));
            expect_arg_sizes({ 7, 1 }, literal_sizer.size(negative_literal));
            expect_arg_sizes({ max_cost, 1 }, literal_sizer.size(saturated_literal));

            const auto &a_div = models.for_script(cardano::script_type::plutus_v1, builtin_semantics::a)
                .builtin_fun.at(builtin_tag::divide_integer);
            value_list empty { alloc };
            expect_equal(1, a_div.mem->cost(arg_sizes { 1, 100 }, empty));
            expect_equal(max_cost, saturated_add(max_cost, 1));
            expect_equal(max_cost, saturated_mul(max_cost, 2));
        };
        "model sizes"_test = [] {
            expect_equal(332, cost_arg_names_v1().size());
            expect_equal(332, cost_arg_names_v2().size());
            expect_equal(350, cost_arg_names_v3().size());
        };
    };
};
