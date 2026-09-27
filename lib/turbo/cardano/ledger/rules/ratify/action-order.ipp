/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Ratify.
// Included at namespace scope by lib/turbo/cardano/conway/governance.cpp.

    bool gov_action_t::delaying() const
    {
        return std::visit<bool>([&](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, no_confidence_t>)
                return true;
            if constexpr (std::is_same_v<T, hard_fork_init_t>)
                return true;
            if constexpr (std::is_same_v<T, update_committee_t>)
                return true;
            if constexpr (std::is_same_v<T, new_constitution_t>)
                return true;
            if constexpr (std::is_same_v<T, treasury_withdrawals_t>)
                return false;
            if constexpr (std::is_same_v<T, parameter_change_t>)
                return false;
            if constexpr (std::is_same_v<T, info_action_t>)
                return false;
            throw error(fmt::format("unsupported gov_action: {}", typeid(T).name()));
            return false;
        }, val);
    }

    int gov_action_t::priority() const
    {
        return std::visit<int>([&](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, no_confidence_t>)
                return 0;
            if constexpr (std::is_same_v<T, update_committee_t>)
                return 1;
            if constexpr (std::is_same_v<T, new_constitution_t>)
                return 2;
            if constexpr (std::is_same_v<T, hard_fork_init_t>)
                return 3;
            if constexpr (std::is_same_v<T, parameter_change_t>)
                return 4;
            if constexpr (std::is_same_v<T, treasury_withdrawals_t>)
                return 5;
            if constexpr (std::is_same_v<T, info_action_t>)
                return 6;
            throw error(fmt::format("unsupported gov_action: {}", typeid(T).name()));
            return std::numeric_limits<int>::max();
        }, val);
    }

    std::strong_ordering gov_action_t::operator<=>(const gov_action_t &o) const
    {
        return priority() <=> o.priority();
    }
