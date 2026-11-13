/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/config.hpp>
#include <turbo/cardano/ledger/rules/utxow/conway-validation.hpp>
#include <turbo/common/test.hpp>
#include <turbo/plutus/cost-model-history.hpp>
#include <turbo/plutus/costs-config.hpp>
#include <turbo/plutus/flat-encoder.hpp>
#include <turbo/plutus/machine.hpp>
#include <turbo/plutus/uplc.hpp>

using namespace turbo;
using namespace turbo::plutus;

suite plutus_dijkstra_suite = [] {
    "plutus::dijkstra"_test = [] {
        "language and Core activation match node 11.2.0"_test = [] {
            using cardano::script_type;
            for (const auto typ: { script_type::plutus_v1, script_type::plutus_v2,
                    script_type::plutus_v3, script_type::plutus_v4 }) {
                expect(!builtins::version_available({1, 2, 0}, typ, 11));
                expect(builtins::version_available({1, 2, 0}, typ, 12));
                expect(!builtins::version_available({1, 3, 0}, typ, 12));
                for (size_t i = 0; i < builtin_tag_count; ++i) {
                    const auto tag = static_cast<builtin_tag>(i);
                    expect(builtins::available(tag, typ, 12) == (builtins::descriptor(tag).batch <= 6));
                }
                const auto expected = typ <= script_type::plutus_v2 ? builtin_semantics::f : builtin_semantics::g;
                expect(builtins::semantics_variant(typ, 12) == expected);
            }
            expect(!builtins::available(builtin_tag::add_integer, script_type::plutus_v4, 11));
            allocator alloc {};
            const uplc::script source { alloc, uint8_vector { std::string_view { "(program 1.2.0 (con unit ()))" } } };
            const auto bytes = flat::encode_cbor(source.version(), source.program());
            expect(throws([&] { flat::script invalid { alloc, bytes, script_type::plutus_v3, 11 }; }));
            const flat::script valid { alloc, bytes, script_type::plutus_v4, 12 };
            expect(valid.version() == source.version());
        };
        "Data case passes one list and respects both version gates and budgets"_test = [] {
            allocator alloc {};
            const uplc::script source { alloc, uint8_vector { std::string_view {
                "(program 1.2.0 (case (con data (Constr 1 [I 7, I 8])) (error) (lam fields fields)))"
            } } };
            const auto &model = costs::defaults().for_script(cardano::script_type::plutus_v3, builtin_semantics::g);
            machine evaluator { alloc, model, cardano::script_type::plutus_v3, {}, 12, source.version() };
            const auto result = evaluator.evaluate(source.program());
            const uplc::script expected { alloc, uint8_vector { std::string_view { "(program 1.2.0 (con (list data) [I 7, I 8]))" } } };
            expect(result.expr == expected.program());
            for (const auto [major, core]: { std::pair { uint64_t { 11 }, version { 1, 2, 0 } },
                    std::pair { uint64_t { 12 }, version { 1, 1, 0 } } }) {
                machine old { alloc, model, cardano::script_type::plutus_v3, {}, major, core };
                expect(throws([&] { old.evaluate(source.program()); }));
            }
            machine exact { alloc, model, cardano::script_type::plutus_v3, result.cost, 12, source.version() };
            expect(exact.evaluate(source.program()).expr == result.expr);
            auto budget = result.cost;
            --budget.steps;
            machine insufficient { alloc, model, cardano::script_type::plutus_v3, budget, 12, source.version() };
            expect(throws([&] { insufficient.evaluate(source.program()); }));
            const uplc::script empty { alloc, uint8_vector { std::string_view {
                "(program 1.2.0 (case (con data (Constr 0 [])) (lam fields fields)))"
            } } };
            machine empty_case { alloc, model, cardano::script_type::plutus_v3, {}, 12, empty.version() };
            const uplc::script empty_expected { alloc, uint8_vector { std::string_view { "(program 1.2.0 (con (list data) []))" } } };
            expect(empty_case.evaluate(empty.program()).expr == empty_expected.program());
            for (const auto scrutinee: { "(I 0)", "(Constr 2 [])", "(Constr -1 [])", "(List [])" }) {
                const uplc::script invalid { alloc, uint8_vector { fmt::format(
                    "(program 1.2.0 (case (con data {}) (lam x x)))", scrutinee) } };
                machine m { alloc, model, cardano::script_type::plutus_v3, {}, 12, invalid.version() };
                expect(throws([&] { m.evaluate(invalid.program()); }));
            }
        };
        "Dijkstra removes the arithmetic integer bound"_test = [] {
            allocator alloc {};
            const cpp_int large = cpp_int { 1 } << 262143;
            const term lhs { alloc, plutus::constant { alloc, bint_type { alloc, large } } };
            const term rhs { alloc, plutus::constant { alloc, bint_type { alloc, 0 } } };
            const term add { alloc, t_builtin { builtin_tag::add_integer } };
            const term expression { alloc, apply { term { alloc, apply { add, lhs } }, rhs } };
            machine old { alloc, cardano::script_type::plutus_v3, {}, 11 };
            expect(throws([&] { old.evaluate(expression); }));
            machine current { alloc, cardano::script_type::plutus_v3, {}, 12 };
            expect(current.evaluate(expression).expr == lhs);
            const term oversized { alloc, plutus::constant { alloc, bstr_type { alloc, uint8_vector(65537, 0) } } };
            const term hash { alloc, apply { term { alloc, t_builtin { builtin_tag::sha2_256 } }, oversized } };
            machine bounded { alloc, cardano::script_type::plutus_v3, {}, 12 };
            expect(throws([&] { bounded.evaluate(hash); }));
        };
        "V4 costs and language views retain the ledger array"_test = [] {
            const configs_dir files { configs_dir::network_path("dijkstra") };
            const cardano::config cfg { files };
            cardano::plutus_cost_models raw {};
            auto values = cfg.dijkstra_genesis->plutus_v4_cost_model;
            values.at(0) = 123456;
            raw.items.emplace(3, cardano::plutus_cost_model { values });
            cost_model_history_t history { { { 0, raw } } };
            const auto &models = history.for_epoch(0);
            const auto &v4 = models.for_script(cardano::script_type::plutus_v4, builtin_semantics::g);
            expect_equal(v4.builtin_costs.at(builtin_tag::add_integer).cpu.args[0], uint64_t { 123456 });
            expect(throws([&] { costs::defaults().for_script(cardano::script_type::plutus_v4, builtin_semantics::g); }));
            allocator alloc {};
            const uplc::script script { alloc, uint8_vector { std::string_view {
                "(program 1.2.0 [[(builtin addInteger) (con integer 2)] (con integer 3)])"
            } }, cardano::script_type::plutus_v4, 12 };
            machine m { alloc, v4, cardano::script_type::plutus_v4, {}, 12, script.version() };
            expect_equal(fmt::format("{}", m.evaluate(script.program()).expr), std::string { "(con integer 5)" });
            const auto views = cardano::ledger::rules::make_language_views(raw);
            cbor::encoder expected {};
            expected.map(1).uint(3).array(values.size());
            for (const auto value: values) {
                if (value >= 0)
                    expected.uint(static_cast<uint64_t>(value));
                else
                    expected.nint(static_cast<uint64_t>(-(value + 1)));
            }
            expect_equal(buffer { views.at(8) }, buffer { expected.cbor() });
            const auto restored = cost_model_history_t::from_json(history.to_json());
            expect_equal(restored.for_epoch(0).for_script(cardano::script_type::plutus_v4,
                builtin_semantics::g).builtin_costs.at(builtin_tag::add_integer).cpu.args[0], uint64_t { 123456 });
            raw.items.at(3) = cardano::plutus_cost_model { { 123456 } };
            const auto short_model = costs::ingest(raw);
            expect_equal(short_model.for_script(cardano::script_type::plutus_v4,
                builtin_semantics::g).builtin_costs.at(builtin_tag::add_integer).cpu.args[1], costs::max_cost);
        };
    };
};
