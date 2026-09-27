#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <utility>
#include <turbo/cardano/ledger/rules/rule-id.hpp>

namespace turbo::cardano::ledger::conway::rules {
    struct no_rule_effect {};

    // Pure rule checks return premise failures as allocation-free values.
    template<typename Effect, typename Failure>
    struct rule_result {
        rule_id rule {};
        Effect effect {};
        Failure failure {};
        bool matched = false;

        constexpr explicit operator bool() const noexcept
        {
            return matched;
        }

        static constexpr rule_result success(const rule_id rule, Effect effect={})
        {
            return { rule, std::move(effect), {}, true };
        }

        static constexpr rule_result fail(const rule_id rule, const Failure failure)
        {
            return { rule, {}, failure, false };
        }
    };

    template<typename Failure>
    using validation_result = rule_result<no_rule_effect, Failure>;
}
