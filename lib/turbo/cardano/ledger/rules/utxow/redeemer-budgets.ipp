/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Utxow.
// Included at namespace scope by lib/turbo/plutus/context.cpp.

    ex_units context::validate_redeemer_budgets(const redeemer_map &redeemers, const ex_units &limit)
    {
        ex_units total {};
        for (const auto &[id, redeemer]: redeemers) {
            if (redeemer.budget.mem > limit.mem - total.mem
                    || redeemer.budget.steps > limit.steps - total.steps) [[unlikely]] {
                throw error(fmt::format(
                    "the total redeemer execution-unit budget exceeds maxTxExUnits {} at {}#{}: "
                    "accumulated {{ mem: {}, steps: {} }}, next {}",
                    limit, id.tag, id.ref_idx, total.mem, total.steps, redeemer.budget));
            }
            total.mem += redeemer.budget.mem;
            total.steps += redeemer.budget.steps;
        }
        return total;
    }
