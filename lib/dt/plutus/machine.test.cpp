/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/common/test.hpp>
#include <dt/plutus/machine.hpp>
#include <dt/plutus/uplc.hpp>
#include <dt/scheduler.hpp>

using namespace daedalus_turbo;
using namespace daedalus_turbo::plutus;

namespace {
    struct script_meta {
        std::string version;
        term expr;
        cardano::ex_units cost {};

        bool operator==(const script_meta &o) const
        {
            return version == o.version
                && *expr == *o.expr
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
            using namespace daedalus_turbo::plutus;
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
        machine m { alloc, costs::defaults().v3.value(), builtins::semantics_v2(), budget };
        return m.evaluate(s.program());
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
                machine m { alloc, costs::defaults().v3.value(), builtins::semantics_v2(), budget };
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
        test_same(path.string(), exp_res, res, loc);
    }

    void test_script_dir(const std::string &script_dir, const optional_budget &budget={})
    {
        for (const auto &path: file::files_with_ext_str(script_dir, ".uplc"))
            test_script(path, budget);
    }
}

suite plutus_machine_suite = [] {
    using plutus::allocator;
    using boost::ext::ut::v2_1_0::nothrow;
    "plutus::machine"_test = [] {
        "discharge updates variable indices"_test = [] {
            const std::string_view uplc { "(program 1.0.0 [(lam v0 (lam v1 v1)) (con bool True)])" };
            allocator alloc {};
            uplc::script s { alloc, write_vector { uplc } };
            machine m { alloc, costs::defaults().v3.value(), builtins::semantics_v2() };
            const auto [res, cost] = m.evaluate(s.program());
            const std::string exp { "(lam v0 v0)" };
            const auto act = fmt::format("{}", res);
            test_same(exp, act);
        };
        "budget"_test = [] {
            allocator alloc {};
            const auto [res, cost] = run_script(alloc, "./data/plutus/conformance/example/factorial/factorial.uplc");
            test_same(50026, cost.mem);
            test_same(9352174, cost.steps);
            // fails with a low cpu budget
            expect(throws([&] { run_script(alloc, "./data/plutus/conformance/example/factorial/factorial.uplc", cardano::ex_units { 50026, 9352173 }); }));
            // fails with a low mem budget
            expect(throws([&] { run_script(alloc, "./data/plutus/conformance/example/factorial/factorial.uplc", cardano::ex_units { 50025, 9352174 }); }));
            // succeeds with a high-enough budget
            expect(nothrow([&] { run_script(alloc, "./data/plutus/conformance/example/factorial/factorial.uplc", cardano::ex_units { 50026, 9352174 }); }));
        };
        "conformance"_test = [] {
            test_script_dir("./data/plutus/conformance/term");
            test_script_dir("./data/plutus/conformance/builtin");
            test_script_dir("./data/plutus/conformance/example");
        };
    };
};