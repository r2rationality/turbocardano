/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <chrono>
#include <nanobench.h>
#include <turbo/common/test.hpp>
#include <turbo/plutus/builtins.hpp>
#include <turbo/plutus/conformance-data.hpp>
#include <turbo/plutus/costs.hpp>
#include <turbo/plutus/machine.hpp>
#include <turbo/plutus/uplc.hpp>

using namespace turbo;
using namespace turbo::plutus;

namespace {
    static constexpr size_t primitive_batch_size = 128;

    term make_unit(allocator &alloc)
    {
        return { alloc, plutus::constant { alloc, std::monostate {} } };
    }

    term make_integer(allocator &alloc, const int64_t val)
    {
        return { alloc, plutus::constant { alloc, bint_type { alloc, val } } };
    }

    term make_bool(allocator &alloc, const bool val)
    {
        return { alloc, plutus::constant { alloc, val } };
    }

    term make_constr(allocator &alloc, term_list::value_type &&args)
    {
        return { alloc, t_constr { 0, term_list { alloc, std::move(args) } } };
    }

    term make_constant_batch(allocator &alloc, const size_t count)
    {
        term_list::value_type args { alloc };
        args.reserve(count);
        for (size_t i = 0; i < count; ++i)
            args.emplace_back(make_unit(alloc));
        return make_constr(alloc, std::move(args));
    }

    term make_lambda_application_chain(allocator &alloc, const size_t count)
    {
        auto expr = make_unit(alloc);
        for (size_t i = count; i-- > 0;) {
            auto fun = term { alloc, t_lambda { expr } };
            expr = term { alloc, apply { std::move(fun), make_unit(alloc) } };
        }
        return expr;
    }

    term make_force_delay_chain(allocator &alloc, const size_t count)
    {
        auto expr = make_unit(alloc);
        for (size_t i = 0; i < count; ++i) {
            auto delayed = term { alloc, t_delay { expr } };
            expr = term { alloc, force { std::move(delayed) } };
        }
        return expr;
    }

    term make_lookup_batch(allocator &alloc, const size_t count, const size_t environment_depth,
            const size_t variable_idx)
    {
        term_list::value_type lookups { alloc };
        lookups.reserve(count);
        for (size_t i = 0; i < count; ++i)
            lookups.emplace_back(alloc, variable { variable_idx });
        auto expr = make_constr(alloc, std::move(lookups));

        for (size_t i = environment_depth; i-- > 0;)
            expr = term { alloc, t_lambda { expr } };
        for (size_t i = 0; i < environment_depth; ++i)
            expr = term { alloc, apply { std::move(expr), make_unit(alloc) } };
        return expr;
    }

    term make_partial_builtin_batch(allocator &alloc, const size_t count)
    {
        term_list::value_type args { alloc };
        args.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            auto fun = term { alloc, t_builtin { builtin_tag::add_integer } };
            args.emplace_back(alloc, apply { std::move(fun), make_integer(alloc, 1) });
        }
        return make_constr(alloc, std::move(args));
    }

    term make_complete_builtin_batch(allocator &alloc, const size_t count)
    {
        term_list::value_type args { alloc };
        args.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            auto fun = term { alloc, t_builtin { builtin_tag::add_integer } };
            auto partial = term { alloc, apply { std::move(fun), make_integer(alloc, 1) } };
            args.emplace_back(alloc, apply { std::move(partial), make_integer(alloc, 2) });
        }
        return make_constr(alloc, std::move(args));
    }

    term make_choose_unit_batch(allocator &alloc, const size_t count, const size_t num_args)
    {
        term_list::value_type args { alloc };
        args.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            auto builtin = term { alloc, t_builtin { builtin_tag::choose_unit } };
            auto expr = term { alloc, force { std::move(builtin) } };
            for (size_t arg_idx = 0; arg_idx < num_args; ++arg_idx)
                expr = term { alloc, apply { std::move(expr), make_unit(alloc) } };
            args.emplace_back(std::move(expr));
        }
        return make_constr(alloc, std::move(args));
    }

    term make_if_then_else_batch(allocator &alloc, const size_t count, const size_t num_args)
    {
        term_list::value_type args { alloc };
        args.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            auto builtin = term { alloc, t_builtin { builtin_tag::if_then_else } };
            auto expr = term { alloc, force { std::move(builtin) } };
            if (num_args > 0)
                expr = term { alloc, apply { std::move(expr), make_bool(alloc, true) } };
            for (size_t arg_idx = 1; arg_idx < num_args; ++arg_idx)
                expr = term { alloc, apply { std::move(expr), make_unit(alloc) } };
            args.emplace_back(std::move(expr));
        }
        return make_constr(alloc, std::move(args));
    }

    term make_choose_data_partial_batch(allocator &alloc, const size_t count, const size_t num_args)
    {
        term_list::value_type vals { alloc };
        vals.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            auto builtin = term { alloc, t_builtin { builtin_tag::choose_data } };
            auto expr = term { alloc, force { std::move(builtin) } };
            for (size_t arg_idx = 0; arg_idx < num_args; ++arg_idx)
                expr = term { alloc, apply { std::move(expr), make_unit(alloc) } };
            vals.emplace_back(expr);
        }
        return make_constr(alloc, std::move(vals));
    }

    term make_lambda_accumulation_batch(allocator &alloc, const size_t count, const size_t num_args)
    {
        term_list::value_type vals { alloc };
        vals.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            auto expr = make_unit(alloc);
            for (size_t arg_idx = num_args; arg_idx-- > 0;)
                expr = term { alloc, t_lambda { std::move(expr) } };
            for (size_t arg_idx = 0; arg_idx < num_args; ++arg_idx)
                expr = term { alloc, apply { std::move(expr), make_unit(alloc) } };
            vals.emplace_back(std::move(expr));
        }
        return make_constr(alloc, std::move(vals));
    }

    term make_case_chain(allocator &alloc, const size_t count)
    {
        auto expr = make_unit(alloc);
        for (size_t i = 0; i < count; ++i) {
            term_list::value_type cases { alloc };
            cases.emplace_back(expr);
            term_list::value_type args { alloc };
            auto arg = make_constr(alloc, std::move(args));
            expr = term { alloc, t_case { std::move(arg), term_list { alloc, std::move(cases) } } };
        }
        return expr;
    }

    void run_machine_benchmark(ankerl::nanobench::Bench &bench, const std::string &name,
            const term &expr, const size_t batch_size)
    {
        // The syntax tree belongs to a separate, longer-lived allocator. Each
        // sample gets a fresh arena so runtime allocations cannot accumulate
        // across nanobench epochs.
        bench.batch(batch_size).run(name, [&] {
            allocator eval_alloc {};
            machine m { eval_alloc };
            m.evaluate_no_res(expr);
        });
    }

    template<typename Action>
    void run_component_benchmark(ankerl::nanobench::Bench &bench, const std::string &name, Action &&action)
    {
        bench.batch(primitive_batch_size).run(name, [&] {
            for (size_t i = 0; i < primitive_batch_size; ++i)
                action();
        });
    }

    value make_repeated_list(allocator &alloc, const constant_type &typ,
            const plutus::constant &element, const size_t count)
    {
        constant_list::list_type vals { alloc };
        vals.reserve(count);
        for (size_t i = 0; i < count; ++i)
            vals.emplace_back(element);
        return value::make_list(alloc, typ, std::move(vals));
    }

    template<typename Action>
    void run_list_operation_benchmark(ankerl::nanobench::Bench &bench,
            const std::string &name, Action &&action)
    {
        // List updates allocate their result. A fresh arena per measured batch
        // avoids accumulating allocations across nanobench epochs while
        // amortizing allocator construction over the batch.
        bench.batch(primitive_batch_size).run(name, [&] {
            allocator result_alloc {};
            for (size_t i = 0; i < primitive_batch_size; ++i) {
                const auto result = action(result_alloc);
                ankerl::nanobench::doNotOptimizeAway(result);
            }
        });
    }

    void run_list_traversal_benchmark(ankerl::nanobench::Bench &bench,
            const std::string &name, const value &list)
    {
        const auto &vals = list.as_list();
        bench.batch(vals.size()).run(name, [&] {
            uintptr_t checksum = 0;
            vals.for_each([&](const auto &val) {
                checksum += reinterpret_cast<uintptr_t>(&val);
            });
            ankerl::nanobench::doNotOptimizeAway(checksum);
        });
    }
}

suite plutus_machine_bench_suite = [] {
    "plutus::machine"_test = [] {
        "switch variant.index() vs std::visit"_test = [] {
            using val_type = std::variant<uint64_t, std::string, uint8_vector>;
            std::vector<val_type> vals { { uint8_vector::from_hex("00112233") }, { "abc" }, { 123ULL } };
            ankerl::nanobench::Bench bench {};
            bench.title("variant dispatch")
                .output(&std::cerr)
                .unit("value")
                .performanceCounters(true)
                .relative(true)
                .batch(vals.size());
            bench.run("switch", [&] {
                uint64_t res = 0;
                for (const auto &val: vals) {
                    switch (val.index()) {
                        case 0: res += sizeof(uint64_t); break;
                        case 1: res += std::get<std::string>(val).size(); break;
                        case 2: res += std::get<uint8_vector>(val).size(); break;
                        default: throw error(fmt::format("unsupported variant index: {}", val.index()));
                    }
                }
                ankerl::nanobench::doNotOptimizeAway(res);
            });
            bench.run("visit", [&] {
                uint64_t res = 0;
                for (const auto &val: vals) {
                    res += std::visit([](const auto &v) {
                        using T = std::decay_t<decltype(v)>;
                        if constexpr (std::is_same_v<T, uint64_t>)
                            return sizeof(v);
                        else
                            return v.size();
                    }, val);
                }
                ankerl::nanobench::doNotOptimizeAway(res);
            });
        };
        "evaluation primitives"_test = [] {
            allocator term_alloc {};
            const auto startup = make_unit(term_alloc);
            const auto constants = make_constant_batch(term_alloc, primitive_batch_size);
            const auto lambda_apps = make_lambda_application_chain(term_alloc, primitive_batch_size);
            const auto force_delays = make_force_delay_chain(term_alloc, primitive_batch_size);
            const auto lookup_tail = make_lookup_batch(term_alloc, primitive_batch_size, 8, 0);
            const auto lookup_depth_8 = make_lookup_batch(term_alloc, primitive_batch_size, 8, 7);
            const auto builtin_partial = make_partial_builtin_batch(term_alloc, primitive_batch_size);
            const auto builtin_complete = make_complete_builtin_batch(term_alloc, primitive_batch_size);
            const auto choose_unit_partial = make_choose_unit_batch(term_alloc, primitive_batch_size, 1);
            const auto choose_unit_complete = make_choose_unit_batch(term_alloc, primitive_batch_size, 2);
            const auto if_then_else_partial = make_if_then_else_batch(term_alloc, primitive_batch_size, 2);
            const auto if_then_else_complete = make_if_then_else_batch(term_alloc, primitive_batch_size, 3);
            const auto choose_data_0_args = make_choose_data_partial_batch(term_alloc, primitive_batch_size, 0);
            const auto choose_data_1_arg = make_choose_data_partial_batch(term_alloc, primitive_batch_size, 1);
            const auto choose_data_2_args = make_choose_data_partial_batch(term_alloc, primitive_batch_size, 2);
            const auto choose_data_3_args = make_choose_data_partial_batch(term_alloc, primitive_batch_size, 3);
            const auto choose_data_4_args = make_choose_data_partial_batch(term_alloc, primitive_batch_size, 4);
            const auto choose_data_5_args = make_choose_data_partial_batch(term_alloc, primitive_batch_size, 5);
            const auto lambda_control_0_args = make_lambda_accumulation_batch(term_alloc, primitive_batch_size, 0);
            const auto lambda_control_1_arg = make_lambda_accumulation_batch(term_alloc, primitive_batch_size, 1);
            const auto lambda_control_2_args = make_lambda_accumulation_batch(term_alloc, primitive_batch_size, 2);
            const auto lambda_control_3_args = make_lambda_accumulation_batch(term_alloc, primitive_batch_size, 3);
            const auto lambda_control_4_args = make_lambda_accumulation_batch(term_alloc, primitive_batch_size, 4);
            const auto lambda_control_5_args = make_lambda_accumulation_batch(term_alloc, primitive_batch_size, 5);
            const auto cases = make_case_chain(term_alloc, primitive_batch_size);

            ankerl::nanobench::Bench bench {};
            bench.title("Plutus machine evaluation primitives")
                .output(&std::cerr)
                .unit("primitive")
                .performanceCounters(true)
                .relative(false)
                .warmup(10)
                .epochs(15)
                .minEpochTime(std::chrono::milliseconds { 20 });

            run_machine_benchmark(bench, "startup + unit constant", startup, 1);
            run_machine_benchmark(bench, "constant under constructor", constants, primitive_batch_size);
            run_machine_benchmark(bench, "lambda application (unused arg)", lambda_apps, primitive_batch_size);
            run_machine_benchmark(bench, "force + delay", force_delays, primitive_batch_size);
            run_machine_benchmark(bench, "variable lookup: 1 hop in depth-8 env", lookup_tail, primitive_batch_size);
            run_machine_benchmark(bench, "variable lookup: 8 hops in depth-8 env", lookup_depth_8, primitive_batch_size);
            run_machine_benchmark(bench, "builtin application: partial add", builtin_partial, primitive_batch_size);
            run_machine_benchmark(bench, "builtin application: complete add", builtin_complete, primitive_batch_size);
            run_machine_benchmark(bench, "builtin application: partial chooseUnit (1/2 args)", choose_unit_partial, primitive_batch_size);
            run_machine_benchmark(bench, "builtin application: complete chooseUnit", choose_unit_complete, primitive_batch_size);
            run_machine_benchmark(bench, "builtin application: partial ifThenElse (2/3 args)", if_then_else_partial, primitive_batch_size);
            run_machine_benchmark(bench, "builtin application: complete ifThenElse", if_then_else_complete, primitive_batch_size);
            run_machine_benchmark(bench, "builtin accumulation: chooseData 0 args", choose_data_0_args, primitive_batch_size);
            run_machine_benchmark(bench, "builtin accumulation: chooseData 1 arg", choose_data_1_arg, primitive_batch_size);
            run_machine_benchmark(bench, "builtin accumulation: chooseData 2 args", choose_data_2_args, primitive_batch_size);
            run_machine_benchmark(bench, "builtin accumulation: chooseData 3 args", choose_data_3_args, primitive_batch_size);
            run_machine_benchmark(bench, "builtin accumulation: chooseData 4 args", choose_data_4_args, primitive_batch_size);
            run_machine_benchmark(bench, "builtin accumulation: chooseData 5 args", choose_data_5_args, primitive_batch_size);
            run_machine_benchmark(bench, "lambda accumulation control: 0 args", lambda_control_0_args, primitive_batch_size);
            run_machine_benchmark(bench, "lambda accumulation control: 1 arg", lambda_control_1_arg, primitive_batch_size);
            run_machine_benchmark(bench, "lambda accumulation control: 2 args", lambda_control_2_args, primitive_batch_size);
            run_machine_benchmark(bench, "lambda accumulation control: 3 args", lambda_control_3_args, primitive_batch_size);
            run_machine_benchmark(bench, "lambda accumulation control: 4 args", lambda_control_4_args, primitive_batch_size);
            run_machine_benchmark(bench, "lambda accumulation control: 5 args", lambda_control_5_args, primitive_batch_size);
            run_machine_benchmark(bench, "constructor case", cases, primitive_batch_size);
        };
        "immutable list storage"_test = [] {
            static constexpr size_t list_extent = 64;

            allocator fixture_alloc {};
            const constant_type integer_type { fixture_alloc, type_tag::integer };
            const plutus::constant element_constant { fixture_alloc, bint_type { fixture_alloc, 1 } };
            const value element { fixture_alloc, element_constant };
            const value drop_32 { fixture_alloc, int64_t { 32 } };

            const auto list_0 = make_repeated_list(fixture_alloc, integer_type, element_constant, 0);
            const auto list_1 = make_repeated_list(fixture_alloc, integer_type, element_constant, 1);
            const auto list_8 = make_repeated_list(fixture_alloc, integer_type, element_constant, 8);
            const auto list_48 = make_repeated_list(fixture_alloc, integer_type, element_constant, 48);
            const auto list_64 = make_repeated_list(fixture_alloc, integer_type, element_constant, list_extent);

            auto prefixed_16 = list_48;
            for (size_t i = 0; i < 16; ++i)
                prefixed_16 = builtins::mk_cons(fixture_alloc, element, prefixed_16);
            auto prefixed_64 = list_0;
            for (size_t i = 0; i < list_extent; ++i)
                prefixed_64 = builtins::mk_cons(fixture_alloc, element, prefixed_64);

            ankerl::nanobench::Bench updates {};
            updates.title("Plutus list update operations")
                .output(&std::cerr)
                .unit("operation")
                .performanceCounters(true)
                .relative(false)
                .warmup(10)
                .epochs(15)
                .minEpochTime(std::chrono::milliseconds { 20 });

            run_list_operation_benchmark(updates, "construct contiguous list: 8 elements", [&](allocator &alloc) {
                return make_repeated_list(alloc, integer_type, element_constant, 8);
            });
            run_list_operation_benchmark(updates, "construct contiguous list: 64 elements", [&](allocator &alloc) {
                return make_repeated_list(alloc, integer_type, element_constant, list_extent);
            });
            run_list_operation_benchmark(updates, "mkCons: empty list", [&](allocator &alloc) {
                return builtins::mk_cons(alloc, element, list_0);
            });
            run_list_operation_benchmark(updates, "mkCons: list length 8", [&](allocator &alloc) {
                return builtins::mk_cons(alloc, element, list_8);
            });
            run_list_operation_benchmark(updates, "mkCons: list length 64", [&](allocator &alloc) {
                return builtins::mk_cons(alloc, element, list_64);
            });
            run_list_operation_benchmark(updates, "tailList: list length 1", [&](allocator &alloc) {
                return builtins::tail_list(alloc, list_1);
            });
            run_list_operation_benchmark(updates, "tailList: list length 8", [&](allocator &alloc) {
                return builtins::tail_list(alloc, list_8);
            });
            run_list_operation_benchmark(updates, "tailList: list length 64", [&](allocator &alloc) {
                return builtins::tail_list(alloc, list_64);
            });
            run_list_operation_benchmark(updates, "tailList: mkCons-built length 64", [&](allocator &alloc) {
                return builtins::tail_list(alloc, prefixed_64);
            });
            run_list_operation_benchmark(updates, "dropList: drop 32 from length 64", [&](allocator &alloc) {
                return builtins::drop_list(alloc, drop_32, list_64);
            });
            run_list_operation_benchmark(updates, "dropList: drop 32 from 16 prepends + tail length 48", [&](allocator &alloc) {
                return builtins::drop_list(alloc, drop_32, prefixed_16);
            });
            run_list_operation_benchmark(updates, "listToArray: contiguous length 64", [&](allocator &alloc) {
                return builtins::list_to_array(alloc, list_64);
            });
            run_list_operation_benchmark(updates, "listToArray: 16 prepends + tail length 48", [&](allocator &alloc) {
                return builtins::list_to_array(alloc, prefixed_16);
            });

            updates.batch(list_extent).run("mkCons chain: 64 prepends", [&] {
                allocator result_alloc {};
                auto list = list_0;
                for (size_t i = 0; i < list_extent; ++i)
                    list = builtins::mk_cons(result_alloc, element, list);
                ankerl::nanobench::doNotOptimizeAway(list);
            });
            updates.batch(list_extent).run("tailList chain: consume length 64", [&] {
                allocator result_alloc {};
                auto list = list_64;
                for (size_t i = 0; i < list_extent; ++i)
                    list = builtins::tail_list(result_alloc, list);
                ankerl::nanobench::doNotOptimizeAway(list);
            });
            updates.batch(list_extent).run("tailList chain: consume 64 prepends", [&] {
                allocator result_alloc {};
                auto list = prefixed_64;
                for (size_t i = 0; i < list_extent; ++i)
                    list = builtins::tail_list(result_alloc, list);
                ankerl::nanobench::doNotOptimizeAway(list);
            });

            ankerl::nanobench::Bench traversal {};
            traversal.title("Plutus list traversal")
                .output(&std::cerr)
                .unit("element")
                .performanceCounters(true)
                .relative(false)
                .warmup(10)
                .epochs(15)
                .minEpochTime(std::chrono::milliseconds { 20 });

            run_list_traversal_benchmark(traversal, "contiguous list: 64 elements", list_64);
            run_list_traversal_benchmark(traversal, "mkCons-built: 16 prepends + tail length 48", prefixed_16);
            run_list_traversal_benchmark(traversal, "mkCons-built: 64 prepends", prefixed_64);
        };
        "builtin completion components"_test = [] {
            allocator arg_alloc {};

            value_list::value_type choose_vals { arg_alloc };
            choose_vals.reserve(2);
            choose_vals.emplace_back(value::unit(arg_alloc));
            choose_vals.emplace_back(value::unit(arg_alloc));
            const value_list choose_args { arg_alloc, std::move(choose_vals) };

            value_list::value_type add_vals { arg_alloc };
            add_vals.reserve(2);
            add_vals.emplace_back(arg_alloc, int64_t { 1 });
            add_vals.emplace_back(arg_alloc, int64_t { 2 });
            const value_list add_args { arg_alloc, std::move(add_vals) };

            const auto &cost_model = costs::defaults().for_script(
                cardano::script_type::plutus_v3, builtin_semantics::c);
            const auto &semantics = builtins::semantics_v2();
            const auto choose_tag = builtin_tag::choose_unit;
            const auto add_tag = builtin_tag::add_integer;
            const auto &choose_op = cost_model.builtin_fun.at(choose_tag);
            const auto &add_op = cost_model.builtin_fun.at(add_tag);
            const auto &choose_info = semantics.at(choose_tag);
            const auto &add_info = semantics.at(add_tag);
            const auto &choose_fun = std::get<builtin_two_arg>(choose_info.func);
            const auto &add_fun = std::get<builtin_two_arg>(add_info.func);

            auto choose_sizes = costs::sizes_for(choose_op, choose_tag, choose_args, false);
            for (size_t i = 0; i < choose_sizes.size(); ++i)
                ankerl::nanobench::doNotOptimizeAway(choose_sizes.at(i));

            ankerl::nanobench::Bench bench {};
            bench.title("Plutus builtin completion components")
                .output(&std::cerr)
                .unit("component")
                .performanceCounters(true)
                .relative(false)
                .warmup(10)
                .epochs(15)
                .minEpochTime(std::chrono::milliseconds { 20 });

            run_component_benchmark(bench, "costing: model lookup", [&] {
                const auto &op = cost_model.builtin_fun.at(choose_tag);
                ankerl::nanobench::doNotOptimizeAway(op.cpu.get());
            });
            run_component_benchmark(bench, "costing: construct argument sizes", [&] {
                auto sizes = costs::sizes_for(choose_op, choose_tag, choose_args, false);
                ankerl::nanobench::doNotOptimizeAway(sizes.size());
            });
            run_component_benchmark(bench, "costing: CPU model with prepared sizes", [&] {
                ankerl::nanobench::doNotOptimizeAway(choose_op.cpu->cost(choose_sizes, choose_args));
            });
            run_component_benchmark(bench, "costing: memory model with prepared sizes", [&] {
                ankerl::nanobench::doNotOptimizeAway(choose_op.mem->cost(choose_sizes, choose_args));
            });
            run_component_benchmark(bench, "costing: complete chooseUnit path", [&] {
                const auto &op = cost_model.builtin_fun.at(choose_tag);
                auto sizes = costs::sizes_for(op, choose_tag, choose_args, false);
                ankerl::nanobench::doNotOptimizeAway(op.cpu->cost(sizes, choose_args));
                ankerl::nanobench::doNotOptimizeAway(op.mem->cost(sizes, choose_args));
            });
            run_component_benchmark(bench, "costing: complete addInteger path", [&] {
                const auto &op = cost_model.builtin_fun.at(add_tag);
                auto sizes = costs::sizes_for(op, add_tag, add_args, false);
                ankerl::nanobench::doNotOptimizeAway(op.cpu->cost(sizes, add_args));
                ankerl::nanobench::doNotOptimizeAway(op.mem->cost(sizes, add_args));
            });
            run_component_benchmark(bench, "semantics: map lookup", [&] {
                const auto &info = semantics.at(choose_tag);
                ankerl::nanobench::doNotOptimizeAway(info.num_args);
            });
            run_component_benchmark(bench, "semantics: direct chooseUnit body", [&] {
                auto res = builtins::choose_unit(arg_alloc, choose_args->at(0), choose_args->at(1));
                ankerl::nanobench::doNotOptimizeAway(res);
            });
            run_component_benchmark(bench, "semantics: prepared chooseUnit function", [&] {
                auto res = choose_fun(arg_alloc, choose_args->at(0), choose_args->at(1));
                ankerl::nanobench::doNotOptimizeAway(res);
            });
            run_component_benchmark(bench, "semantics: lookup + dispatch + chooseUnit", [&] {
                const auto &info = semantics.at(choose_tag);
                const auto &fun = std::get<builtin_two_arg>(info.func);
                auto res = fun(arg_alloc, choose_args->at(0), choose_args->at(1));
                ankerl::nanobench::doNotOptimizeAway(res);
            });
            bench.batch(primitive_batch_size).run("semantics: direct addInteger body", [&] {
                allocator result_alloc {};
                for (size_t i = 0; i < primitive_batch_size; ++i) {
                    auto res = builtins::add_integer(result_alloc, add_args->at(0), add_args->at(1));
                    ankerl::nanobench::doNotOptimizeAway(res);
                }
            });
            bench.batch(primitive_batch_size).run("semantics: prepared addInteger function", [&] {
                allocator result_alloc {};
                for (size_t i = 0; i < primitive_batch_size; ++i) {
                    auto res = add_fun(result_alloc, add_args->at(0), add_args->at(1));
                    ankerl::nanobench::doNotOptimizeAway(res);
                }
            });
        };
        "conformance examples"_test = [] {
            allocator script_alloc {};
            std::vector<uplc::script> scripts {};
            const auto example_dir = conformance_data_dir() / "example";
            for (const auto &path: file::files_with_ext_path(example_dir.string(), ".uplc")) {
                if (!path.stem().string().starts_with("DivideByZero"))
                    scripts.emplace_back(script_alloc, file::read(path.string()));
            }
            const auto eval = [&] {
                uint64_t total_steps = 0;
                for (const auto &s: scripts) {
                    allocator eval_alloc {};
                    machine m { eval_alloc };
                    const auto res = m.evaluate(s.program());
                    total_steps += res.cost.steps;
                }
                return total_steps;
            };
            const auto total_steps = eval();
            ankerl::nanobench::Bench bench {};
            bench.title("Plutus conformance examples")
                .output(&std::cerr)
                .unit("CPU step")
                .performanceCounters(true)
                .batch(total_steps)
                .run("evaluate", [&] {
                    ankerl::nanobench::doNotOptimizeAway(eval());
                });
        };
    };
};
