/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Script.Validation.
// Included at namespace scope by lib/turbo/plutus/context.cpp.

    ex_units context::eval_script(prepared_script &ps) const
    {
        try {
            const auto &pv = _require_protocol_ver();
            machine m { ps.alloc, cost_models().for_script(ps.typ, builtins::semantics_variant(ps.typ, pv.major)), ps.typ, ps.budget, pv.major };
            return m.evaluate_no_res(ps.expr);
        } catch (const error &ex) {
            throw error(fmt::format("script {} {}: {}", ps.typ, ps.hash, ex.what()));
        }
    }
