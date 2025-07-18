#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/plutus/builtins.hpp>
#include <turbo/plutus/costs.hpp>
#include <turbo/plutus/types.hpp>

namespace turbo::plutus {
    using optional_budget = std::optional<cardano::ex_units>;

    struct machine {
        struct result {
            const term expr;
            const cardano::ex_units cost;

            bool operator==(const result& o) const
            {
                return *expr == *o.expr && cost == o.cost;
            }
        };

        machine(allocator &alloc, cardano::script_type typ, const optional_budget &budget={});
        machine(allocator &alloc, const costs::parsed_model &model=costs::defaults().v3.value(),
                const builtin_map &semantics=builtins::semantics_v2(), const optional_budget &budget={});
        ~machine();
        //term apply_args(const term &expr, const term_list &args);
        result evaluate(const term &expr);
        void evaluate_no_res(const term &expr);
    private:
        struct impl;
        std::unique_ptr<impl> _impl;
    };
}