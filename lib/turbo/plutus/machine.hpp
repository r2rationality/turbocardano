#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/plutus/builtins.hpp>
#include <turbo/plutus/costs.hpp>
#include <turbo/plutus/types.hpp>

namespace turbo::plutus {
    using optional_budget = std::optional<cardano::ex_units>;

    struct machine {
        static constexpr uint64_t builtin_case_protocol_major = 11;

        struct result {
            const term expr;
            const cardano::ex_units cost;

            bool operator==(const result& o) const
            {
                return *expr == *o.expr && cost == o.cost;
            }
        };

        machine(allocator &alloc, cardano::script_type typ, const optional_budget &budget={},
                uint64_t protocol_major=builtin_case_protocol_major);
        machine(allocator &alloc, const costs::parsed_model &model, cardano::script_type typ,
                const optional_budget &budget={}, uint64_t protocol_major=builtin_case_protocol_major);
        machine(allocator &alloc, const costs::parsed_model &model=costs::defaults().v3.value(),
                const builtin_map &semantics=builtins::semantics_v2(), const optional_budget &budget={},
                uint64_t protocol_major=builtin_case_protocol_major,
                builtin_semantics semantics_variant=builtin_semantics::c);
        ~machine();
        //term apply_args(const term &expr, const term_list &args);
        result evaluate(const term &expr);
        void evaluate_no_res(const term &expr);
    private:
        struct impl;
        std::unique_ptr<impl> _impl;
    };
}