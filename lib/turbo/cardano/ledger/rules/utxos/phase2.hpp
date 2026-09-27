#pragma once
/* Copyright (c) 2026 R2 Rationality OÜ. License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */
#include <turbo/common/error.hpp>

namespace turbo::cardano::ledger::rules {
    template<typename Redeemers, typename Prepare, typename Evaluate>
    void validate_phase2_result(const Redeemers &redeemers, bool expected_valid,
        Prepare &&prepare, Evaluate &&evaluate)
    {
        bool success = true;
        for (const auto &[id, redeemer]: redeemers) {
            // Prepare every invocation even after failure. Phase-1/preparation errors
            // must not be mistaken for an acceptable phase-2 false result.
            auto program = prepare(redeemer);
            if (success) {
                try {
                    evaluate(program);
                } catch (const error &) {
                    success = false;
                }
            }
        }
        if (success != expected_valid)
            throw error("phase-2 result does not match the transaction validity flag");
    }
}
