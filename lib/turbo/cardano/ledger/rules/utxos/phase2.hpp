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
            // Preparation errors must not count as an acceptable phase-2 false result.
            auto program = prepare(redeemer);
            if (success) {
                try {
                    evaluate(program);
                } catch (const error &ex) {
                    if (expected_valid) [[unlikely]]
                        throw error{fmt::format("phase-2 evaluation failed for redeemer {} although the transaction is marked valid", id), ex};
                    success = false;
                }
            }
        }
        if (success != expected_valid) [[unlikely]]
            throw error{"phase-2 scripts succeeded but the transaction is marked invalid"};
    }
}
