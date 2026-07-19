/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/scheduler.hpp>
#include <turbo/common/test.hpp>
#include <turbo/plutus/conformance-data.hpp>
#include <turbo/plutus/costs-config.hpp>
#include <turbo/plutus/flat-encoder.hpp>
#include <turbo/plutus/flat.hpp>
#include <turbo/plutus/machine.hpp>
#include <turbo/plutus/uplc.hpp>

using namespace turbo;
using namespace turbo::plutus;

namespace {
    struct script_meta {
        std::string version;
        term expr;
        cardano::ex_units cost {};

        bool operator==(const script_meta &o) const
        {
            return version == o.version
                && expr == o.expr
                && cost == o.cost;
        }
    };
    using parse_res = std::variant<script_meta, std::string>;
}

namespace fmt {
    template<>
    struct formatter<parse_res>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using namespace turbo::plutus;
            switch (v.index()) {
                case 0: {
                    const auto &s = std::get<script_meta>(v);
                    return fmt::format_to(ctx.out(), "(program {} {}) (cost: {})", s.version, s.expr, s.cost);
                }
                case 1: return fmt::format_to(ctx.out(), "{}", std::get<std::string>(v));
                default: throw error(fmt::format("unsupported script_info index: {}", v.index()));
            }
        }
    };
}

namespace {
    static constexpr uint64_t conformance_protocol_major = machine::builtin_case_protocol_major;

    const costs::runtime_model &conformance_cost_model()
    {
        // The upstream corpus is generated with Plutus' testing defaults, not
        // with the node configuration's currently active ledger parameters.
        static const auto models = costs::ingest({});
        return models.for_script(cardano::script_type::plutus_v3, builtin_semantics::e);
    }

    cardano::ex_units parse_budget(const std::string &path)
    {
        cardano::ex_units budget {};
        const std::string text { file::read(path).str() };
        if (!text.starts_with("({cpu: ")) [[unlikely]]
            throw error(fmt::format("unsupported budget format: {}", text));
        const auto eol_pos = text.find('\n');
        if (eol_pos == std::string::npos) [[unlikely]]
            throw error(fmt::format("unsupported budget format: {}", text));
        budget.steps = std::stoull(text.substr(7, eol_pos - 7));
        const auto line2 = text.substr(eol_pos + 1);
        if (!line2.starts_with("| mem: ")) [[unlikely]]
            throw error(fmt::format("unsupported budget format: {}", text));
        const auto rbr_pos = line2.find('}');
        if (rbr_pos == std::string::npos) [[unlikely]]
            throw error(fmt::format("unsupported budget format: {}", text));
        budget.mem = std::stoull(line2.substr(7, rbr_pos - 7));
        return budget;
    }

    parse_res parse_script(plutus::allocator &alloc, const std::string &path, const std::function<std::string(const std::string &)> &on_error)
    {
        try {
            uplc::script s { alloc, file::read(path) };
            return script_meta { s.version(), s.program() };
        } catch (...) {
            return on_error(path);
        }
    }

    machine::result run_script(plutus::allocator &alloc, const std::string &path, const optional_budget &budget={})
    {
        const uplc::script s { alloc, file::read(path) };
        machine m { alloc, conformance_cost_model(), cardano::script_type::plutus_v3, budget,
            conformance_protocol_major };
        return m.evaluate(s.program());
    }

    std::string eval_uplc(const std::string_view src, const uint64_t protocol_major)
    {
        plutus::allocator alloc {};
        const uplc::script s { alloc, uint8_vector { src } };
        machine m { alloc, cardano::script_type::plutus_v3, {}, protocol_major };
        return fmt::format("{}", m.evaluate(s.program()).expr);
    }

    std::string eval_profile(const std::string_view src, const cardano::script_type typ,
            const uint64_t protocol_major)
    {
        plutus::allocator alloc {};
        const uplc::script s { alloc, uint8_vector { src }, typ, protocol_major };
        machine m { alloc, typ, {}, protocol_major };
        return fmt::format("{}", m.evaluate(s.program()).expr);
    }

    void test_script(const std::filesystem::path &path, const optional_budget &budget={}, const reflection::source_location &loc=reflection::source_location::current())
    {
        plutus::allocator alloc {};
        const auto exp_path = fmt::format("{}.uplc.expected", (path.parent_path() / path.stem()).string());
        auto exp_res = parse_script(alloc, exp_path, [](const auto &p) { return std::string { file::read(p).str() }; });
        if (std::holds_alternative<script_meta>(exp_res)) {
            auto &si = std::get<script_meta>(exp_res);
            si.cost = parse_budget(fmt::format("{}.uplc.budget.expected", (path.parent_path() / path.stem()).string()));
        }
        auto res = parse_script(alloc, path.string(), [](const auto &) { return "parse error"; });
        std::optional<std::string> eval_err {};
        if (std::holds_alternative<script_meta>(res)) {
            try {
                auto &si = std::get<script_meta>(res);
                machine m { alloc, conformance_cost_model(), cardano::script_type::plutus_v3, budget,
                    conformance_protocol_major };
                auto [res, cost] = m.evaluate(si.expr);
                si.expr = std::move(res);
                si.cost = std::move(cost);
            } catch (const std::exception &ex) {
                res = "evaluation failure";
                eval_err.emplace(ex.what());
            } catch (...) {
                res = "evaluation failure";
                eval_err.emplace("unknown error");
            }
        }
        expect_equal(exp_res, res, path.string(), loc);
    }

    void test_script_dir(const std::string &script_dir, const optional_budget &budget={})
    {
        for (const auto &path: file::files_with_ext(script_dir, ".uplc"))
            test_script(path, budget);
    }
}

suite plutus_machine_suite = [] {
    using plutus::allocator;
    using boost::ext::ut::v2_1_0::nothrow;
    "plutus::machine"_test = [] {
        "Flat builtin spines preserve evaluation and costing"_test = [] {
            allocator source_alloc {};
            const uplc::script source {
                source_alloc,
                uint8_vector { std::string_view {
                    "(program 1.0.0 [[(builtin addInteger) (con integer 1)] (con integer 2)])"
                } }
            };
            const auto bytes = flat::encode(source.version(), source.program());
            allocator decode_alloc {};
            const flat::script decoded { decode_alloc, buffer { bytes }, false };

            allocator generic_eval_alloc {};
            machine generic { generic_eval_alloc, cardano::script_type::plutus_v3 };
            const auto generic_result = generic.evaluate(source.program());
            allocator spine_eval_alloc {};
            machine spine { spine_eval_alloc, cardano::script_type::plutus_v3 };
            const auto spine_result = spine.evaluate(decoded.program());
            expect_equal(generic_result.expr, spine_result.expr);
            expect_equal(generic_result.cost, spine_result.cost);

            allocator partial_source_alloc {};
            const uplc::script partial_source {
                partial_source_alloc,
                uint8_vector { std::string_view {
                    "(program 1.0.0 [(builtin addInteger) (con integer 1)])"
                } }
            };
            const auto partial_bytes = flat::encode(
                partial_source.version(), partial_source.program());
            allocator partial_decode_alloc {};
            const flat::script partial_decoded {
                partial_decode_alloc, buffer { partial_bytes }, false
            };
            allocator partial_generic_eval_alloc {};
            machine partial_generic { partial_generic_eval_alloc, cardano::script_type::plutus_v3 };
            const auto partial_generic_result = partial_generic.evaluate(partial_source.program());
            allocator partial_spine_eval_alloc {};
            machine partial_spine { partial_spine_eval_alloc, cardano::script_type::plutus_v3 };
            const auto partial_spine_result = partial_spine.evaluate(partial_decoded.program());
            expect_equal(partial_generic_result.expr, partial_spine_result.expr);
            expect_equal(partial_generic_result.cost, partial_spine_result.cost);

            allocator budget_eval_alloc {};
            machine exact_budget { budget_eval_alloc, cardano::script_type::plutus_v3,
                spine_result.cost };
            expect(nothrow([&] { exact_budget.evaluate(decoded.program()); }));
            allocator low_budget_eval_alloc {};
            machine low_budget { low_budget_eval_alloc, cardano::script_type::plutus_v3,
                cardano::ex_units { spine_result.cost.mem, spine_result.cost.steps - 1 } };
            expect(throws([&] { low_budget.evaluate(decoded.program()); }));
        };
        "discharge updates variable indices"_test = [] {
            const std::string_view uplc { "(program 1.0.0 [(lam v0 (lam v1 v1)) (con bool True)])" };
            allocator alloc {};
            uplc::script s { alloc, uint8_vector { uplc } };
            machine m { alloc, cardano::script_type::plutus_v3 };
            const auto [res, cost] = m.evaluate(s.program());
            const std::string exp { "(lam v0 v0)" };
            const auto act = fmt::format("{}", res);
            expect_equal(exp, act);
        };
        "discharge substitutes a captured outer binding"_test = [] {
            const std::string_view uplc { "(program 1.0.0 [(lam v0 (lam v1 v0)) (con bool True)])" };
            allocator alloc {};
            uplc::script s { alloc, uint8_vector { uplc } };
            machine m { alloc, cardano::script_type::plutus_v3 };
            const auto [res, cost] = m.evaluate(s.program());
            const std::string exp { "(lam v0 (con bool True))" };
            const auto act = fmt::format("{}", res);
            expect_equal(exp, act);
        };
        "budget"_test = [] {
            allocator alloc {};
            const auto factorial = (conformance_data_dir() / "example/factorial/factorial.uplc").string();
            const auto [res, cost] = run_script(alloc, factorial);
            expect_equal(50026, cost.mem);
            expect_equal(9352174, cost.steps);
            // fails with a low cpu budget
            expect(throws([&] { run_script(alloc, factorial, cardano::ex_units { 50026, 9352173 }); }));
            // fails with a low mem budget
            expect(throws([&] { run_script(alloc, factorial, cardano::ex_units { 50025, 9352174 }); }));
            // succeeds with a high-enough budget
            expect(nothrow([&] { run_script(alloc, factorial, cardano::ex_units { 50026, 9352174 }); }));
        };
        "batched fixed-step accounting"_test = [] {
            allocator alloc {};
            auto model = costs::defaults().for_script(cardano::script_type::plutus_v3, builtin_semantics::e);
            model.startup_op = { 1, 2 };
            model.force_op = { 3, 5 };
            model.delay_op = { 7, 11 };
            model.constant_op = { 13, 17 };

            term expr { alloc, plutus::constant { alloc, std::monostate {} } };
            // 101 force/delay pairs plus the constant cross the 200-step checkpoint
            // and leave three steps to be charged at normal evaluation completion.
            for (size_t i = 0; i < 101; ++i) {
                auto delayed = term { alloc, t_delay { std::move(expr) } };
                expr = term { alloc, force { std::move(delayed) } };
            }

            machine unbudgeted { alloc, model, cardano::script_type::plutus_v3, {},
                conformance_protocol_major };
            const auto [res, cost] = unbudgeted.evaluate(expr);
            expect_equal(cardano::ex_units { 1024, 1635 }, cost);

            machine exact { alloc, model, cardano::script_type::plutus_v3,
                cardano::ex_units { 1024, 1635 }, conformance_protocol_major };
            expect(nothrow([&] { exact.evaluate(expr); }));

            machine low_cpu { alloc, model, cardano::script_type::plutus_v3,
                cardano::ex_units { 1024, 1634 }, conformance_protocol_major };
            expect(throws([&] { low_cpu.evaluate(expr); }));

            machine low_mem { alloc, model, cardano::script_type::plutus_v3,
                cardano::ex_units { 1023, 1635 }, conformance_protocol_major };
            expect(throws([&] { low_mem.evaluate(expr); }));
        };
        "protocol 11 builtin case"_test = [] {
            expect_equal("(con integer 10)", eval_uplc("(program 1.1.0 (case (con unit ()) (con integer 10)))", 11));
            expect_equal("(con integer 20)", eval_uplc("(program 1.1.0 (case (con bool True) (con integer 10) (con integer 20)))", 11));
            expect_equal("(con integer 20)", eval_uplc("(program 1.1.0 (case (con integer 1) (con integer 10) (con integer 20)))", 11));
            expect_equal("(con integer 7)", eval_uplc("(program 1.1.0 (case (con (list integer) [7, 8]) (lam h (lam t h)) (con integer 0)))", 11));
            expect_equal("(con integer 0)", eval_uplc("(program 1.1.0 (case (con (list integer) []) (lam h (lam t h)) (con integer 0)))", 11));
            expect_equal("(con integer 4)", eval_uplc("(program 1.1.0 (case (con (pair integer integer) (3, 4)) (lam a (lam b b))))", 11));
        };
        "pre-protocol 11 case"_test = [] {
            const std::initializer_list<std::string_view> builtin_cases {
                "(program 1.1.0 (case (con unit ()) (con integer 10)))",
                "(program 1.1.0 (case (con bool False) (con integer 10)))",
                "(program 1.1.0 (case (con integer 0) (con integer 10)))",
                "(program 1.1.0 (case (con (list integer) []) (lam h (lam t h)) (con integer 0)))",
                "(program 1.1.0 (case (con (pair integer integer) (3, 4)) (lam a (lam b b))))"
            };
            for (const uint64_t protocol_major: { 9, 10 }) {
                expect_equal("(con integer 2)", eval_uplc("(program 1.1.0 (case (constr 0 (con integer 2)) (lam x x)))", protocol_major));
                for (const auto src: builtin_cases)
                    expect(throws([&] { eval_uplc(src, protocol_major); }));
            }
        };
        "builtin semantics variants"_test = [] {
            using cardano::script_type;
            expect(builtins::semantics_variant(script_type::plutus_v1, 8) == builtin_semantics::a);
            expect(builtins::semantics_variant(script_type::plutus_v2, 9) == builtin_semantics::b);
            expect(builtins::semantics_variant(script_type::plutus_v3, 9) == builtin_semantics::c);
            expect(builtins::semantics_variant(script_type::plutus_v1, 11) == builtin_semantics::d);
            expect(builtins::semantics_variant(script_type::plutus_v3, 11) == builtin_semantics::e);

            const std::string_view cons_out_of_range {
                "(program 1.0.0 [[(builtin consByteString) (con integer 256)] (con bytestring #)])"
            };
            expect(nothrow([&] { eval_profile(cons_out_of_range, script_type::plutus_v1, 8); }));
            expect(throws([&] { eval_profile(cons_out_of_range, script_type::plutus_v3, 9); }));
            expect(nothrow([&] { eval_profile(cons_out_of_range, script_type::plutus_v1, 11); }));
            expect(throws([&] { eval_profile(cons_out_of_range, script_type::plutus_v3, 11); }));

            const std::string_view constr_data_out_of_range {
                "(program 1.0.0 [[(builtin constrData) (con integer -1)] (con (list data) [])])"
            };
            expect(nothrow([&] { eval_profile(constr_data_out_of_range, script_type::plutus_v3, 10); }));
            expect(throws([&] { eval_profile(constr_data_out_of_range, script_type::plutus_v3, 11); }));
        };
        "builtin availability by ledger language and protocol"_test = [] {
            using cardano::script_type;
            expect(!builtins::available(builtin_tag::add_integer, script_type::plutus_v1, 4));
            expect(builtins::available(builtin_tag::add_integer, script_type::plutus_v1, 5));
            expect(!builtins::available(builtin_tag::serialise_data, script_type::plutus_v1, 10));
            expect(builtins::available(builtin_tag::serialise_data, script_type::plutus_v1, 11));

            expect(builtins::available(builtin_tag::serialise_data, script_type::plutus_v2, 7));
            expect(!builtins::available(builtin_tag::verify_ecdsa_secp_256k1_signature, script_type::plutus_v2, 7));
            expect(builtins::available(builtin_tag::verify_ecdsa_secp_256k1_signature, script_type::plutus_v2, 8));
            expect(builtins::available(builtin_tag::integer_to_byte_string, script_type::plutus_v2, 10));
            expect(!builtins::available(builtin_tag::bls12_381_g1_add, script_type::plutus_v2, 10));
            expect(builtins::available(builtin_tag::bls12_381_g1_add, script_type::plutus_v2, 11));

            expect(builtins::available(builtin_tag::bls12_381_g1_add, script_type::plutus_v3, 9));
            expect(!builtins::available(builtin_tag::and_byte_string, script_type::plutus_v3, 9));
            expect(builtins::available(builtin_tag::and_byte_string, script_type::plutus_v3, 10));
            expect(!builtins::available(builtin_tag::exp_mod_integer, script_type::plutus_v3, 10));
            expect(builtins::available(builtin_tag::exp_mod_integer, script_type::plutus_v3, 11));
            expect(!builtins::available(builtin_tag::insert_coin, script_type::plutus_v3, 10));
            expect(builtins::available(builtin_tag::insert_coin, script_type::plutus_v3, 11));

            const std::string_view dead_batch_6 { "(program 1.0.0 (delay (builtin expModInteger)))" };
            expect(throws([&] { eval_profile(dead_batch_6, script_type::plutus_v3, 10); }));
            expect(nothrow([&] { eval_profile(dead_batch_6, script_type::plutus_v3, 11); }));

            for (const auto constant: {
                    std::string_view { "(program 1.0.0 (con (array integer) []))" },
                    std::string_view { "(program 1.0.0 (con value []))" }
            }) {
                expect(throws([&] { eval_profile(constant, script_type::plutus_v3, 10); }));
                expect(nothrow([&] { eval_profile(constant, script_type::plutus_v3, 11); }));
            }
        };
        "UPLC versions by ledger language and protocol"_test = [] {
            using cardano::script_type;
            expect(nothrow([&] {
                eval_profile("(program 1.0.0 (con unit ()))", script_type::plutus_v1, 10);
            }));
            expect(throws([&] {
                eval_profile("(program 1.1.0 (con unit ()))", script_type::plutus_v1, 10);
            }));
            expect(nothrow([&] {
                eval_profile("(program 1.1.0 (con unit ()))", script_type::plutus_v1, 11);
            }));
            expect(nothrow([&] {
                eval_profile("(program 1.1.0 (con unit ()))", script_type::plutus_v3, 9);
            }));
            expect(throws([&] {
                eval_profile("(program 1.2.0 (con unit ()))", script_type::plutus_v3, 11);
            }));
            expect(throws([&] {
                eval_profile("(program 1.0.0 (con unit ()))", script_type::plutus_v2, 6);
            }));
        };
        "protocol 11 Flat bounds"_test = [] {
            {
                allocator alloc {};
                term_list::value_type args { alloc };
                for (size_t i = 0; i < 1025; ++i)
                    args.emplace_back(term { alloc, plutus::constant { alloc, std::monostate {} } });
                const term expr { alloc, t_constr { 0, term_list { alloc, std::move(args) } } };
                expect(throws([&] {
                    allocator decode_alloc {};
                    flat::script s { decode_alloc, flat::encode(version { 1, 1, 0 }, expr),
                        cardano::script_type::plutus_v3, 11, false };
                }));
            }
            {
                allocator alloc {};
                constant_type elem_type { alloc, type_tag::unit };
                for (size_t i = 0; i < 15; ++i) {
                    constant_type::list_type nested { alloc };
                    nested.emplace_back(elem_type);
                    elem_type = constant_type { alloc, type_tag::list, std::move(nested) };
                }
                const term expr { alloc, plutus::constant { alloc, constant_list { alloc, elem_type } } };
                expect(throws([&] {
                    allocator decode_alloc {};
                    flat::script s { decode_alloc, flat::encode(version { 1, 1, 0 }, expr),
                        cardano::script_type::plutus_v3, 11, false };
                }));
            }
        };
        "conformance"_test = [] {
            const auto &root = conformance_data_dir();
            test_script_dir((root / "term").string());
            test_script_dir((root / "builtin").string());
            test_script_dir((root / "example").string());
        };
    };
};
