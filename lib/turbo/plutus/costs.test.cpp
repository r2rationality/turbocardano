/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/test.hpp>
#include <turbo/plutus/costs-config.hpp>
#include <turbo/plutus/machine.hpp>

using namespace turbo;
using namespace turbo::plutus;
using namespace turbo::plutus::costs;

namespace {
    value sized_integer(allocator &alloc, const size_t words)
    {
        cpp_int n { 1 };
        if (words > 1)
            n <<= 64 * (words - 1);
        return value { alloc, std::move(n) };
    }
}

suite plutus_costs_suite = [] {
    "plutus::costs"_test = [] {
        "runtime models contain only prepared formulas"_test = [] {
            static_assert(std::is_trivially_copyable_v<runtime_cost>);
            static_assert(std::is_trivially_copyable_v<builtin_cost>);
            static_assert(sizeof(runtime_cost) == 64);

            const auto &model = defaults().for_script(
                cardano::script_type::plutus_v3, builtin_semantics::e);
            expect(model.builtin_costs.at(builtin_tag::choose_unit).cpu.kind
                == runtime_cost_kind::constant);
            expect(model.builtin_costs.at(builtin_tag::divide_integer).cpu.kind
                != runtime_cost_kind::invalid);

            const auto &c_divide = defaults().for_script(
                cardano::script_type::plutus_v3, builtin_semantics::c)
                .builtin_costs.at(builtin_tag::divide_integer);
            expect(c_divide.cpu.kind == runtime_cost_kind::const_above_diagonal);
            expect(c_divide.cpu.nested_kind == runtime_cost_kind::quadratic_in_x_and_y);
            expect(c_divide.cpu_diagonal_constant > 0);
        };
        "defaults"_test = [] {
            // Full cost and budget equivalence is covered by the Plutus conformance suite. These
            // checks exercise the execution-ready API without retaining an independent runtime
            // implementation of the cost model.
            allocator alloc {};
            const auto &v3 = defaults().for_script(
                cardano::script_type::plutus_v3, builtin_semantics::c);
            const auto &div = v3.builtin_costs.at(builtin_tag::divide_integer);
            const value_list small { alloc, {
                sized_integer(alloc, 1), sized_integer(alloc, 1)
            } };
            const value_list large { alloc, {
                sized_integer(alloc, 100), sized_integer(alloc, 100)
            } };
            expect_equal(cardano::ex_units { 1, 131930 },
                cost_builtin(div, builtin_tag::divide_integer, small, false));
            expect_equal(cardano::ex_units { 1, 85848 },
                cost_builtin(div, builtin_tag::divide_integer, large, false));

            const auto &v2 = defaults().for_script(
                cardano::script_type::plutus_v2, builtin_semantics::b);
            const auto &equals_data = v2.builtin_costs.at(builtin_tag::equals_data);
            const value arg1 { alloc, data::constr(alloc, 0, {
                data::constr(alloc, 1, { data::bstr(alloc, uint8_vector::from_hex("AABB")) })
            }) };
            const value arg2 { alloc, data::constr(alloc, 0, {
                data::constr(alloc, 1, { data::bstr(alloc, uint8_vector::from_hex("DDDD")) })
            }) };
            const value_list args { alloc, { arg1, arg2 } };
            expect_equal(cardano::ex_units { 1, 1252775 },
                cost_builtin(equals_data, builtin_tag::equals_data, args, false));
        };
        "string sizes follow semantics variant"_test = [] {
            allocator alloc {};
            const value_list args { alloc, {
                value { alloc, std::string_view { "\xC3\xA9\xC3\xA9" } }
            } };
            const auto &op = defaults().for_script(cardano::script_type::plutus_v3,
                builtin_semantics::c).builtin_costs.at(builtin_tag::encode_utf8);
            const auto code_point_cost = cost_builtin(op, builtin_tag::encode_utf8, args, false);
            const auto byte_length_cost = cost_builtin(op, builtin_tag::encode_utf8, args, true);
            expect(code_point_cost.steps > byte_length_cost.steps);
        };
        "nested and signed formulas are prepared during ingestion"_test = [] {
            const cardano::plutus_cost_models no_overrides {};
            const auto models = ingest(no_overrides);
            expect(throws([&] {
                models.for_script(cardano::script_type::plutus_v3, builtin_semantics::a);
            }));

            allocator alloc {};
            const value_list ascending { alloc, {
                sized_integer(alloc, 1), sized_integer(alloc, 100)
            } };
            const value_list descending { alloc, {
                sized_integer(alloc, 100), sized_integer(alloc, 1)
            } };
            const auto &d_div = models.for_script(cardano::script_type::plutus_v1,
                builtin_semantics::d).builtin_costs.at(builtin_tag::divide_integer);
            const auto ascending_cost = cost_builtin(
                d_div, builtin_tag::divide_integer, ascending, false);
            const auto descending_cost = cost_builtin(
                d_div, builtin_tag::divide_integer, descending, false);
            expect_equal(ascending_cost.steps, descending_cost.steps);

            const auto &exp_mod = models.for_script(cardano::script_type::plutus_v3,
                builtin_semantics::e).builtin_costs.at(builtin_tag::exp_mod_integer);
            const value_list small_base { alloc, {
                sized_integer(alloc, 1), sized_integer(alloc, 2), sized_integer(alloc, 3)
            } };
            const value_list large_base { alloc, {
                sized_integer(alloc, 4), sized_integer(alloc, 2), sized_integer(alloc, 3)
            } };
            expect_equal(cardano::ex_units { 3, 2953927 },
                cost_builtin(exp_mod, builtin_tag::exp_mod_integer, small_base, false));
            expect_equal(cardano::ex_units { 3, 4430890 },
                cost_builtin(exp_mod, builtin_tag::exp_mod_integer, large_base, false));
        };
        "custom sizing and saturation"_test = [] {
            const cardano::plutus_cost_models no_overrides {};
            const auto models = ingest(no_overrides);
            allocator alloc {};

            const auto &replicate = models.for_script(cardano::script_type::plutus_v1,
                builtin_semantics::d).builtin_costs.at(builtin_tag::replicate_byte);
            const value_list negative { alloc, {
                value { alloc, cpp_int { -9 } }, value { alloc, cpp_int { 0 } }
            } };
            const value_list positive { alloc, {
                value { alloc, cpp_int { 9 } }, value { alloc, cpp_int { 0 } }
            } };
            expect_equal(cost_builtin(replicate, builtin_tag::replicate_byte, negative, false),
                cost_builtin(replicate, builtin_tag::replicate_byte, positive, false));

            const auto &a_div = models.for_script(cardano::script_type::plutus_v1,
                builtin_semantics::a).builtin_costs.at(builtin_tag::divide_integer);
            const value_list uneven { alloc, {
                sized_integer(alloc, 1), sized_integer(alloc, 100)
            } };
            expect_equal(uint64_t { 1 },
                cost_builtin(a_div, builtin_tag::divide_integer, uneven, false).mem);
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
