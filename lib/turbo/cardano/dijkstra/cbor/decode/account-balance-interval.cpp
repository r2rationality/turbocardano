/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/transaction.hpp>

namespace turbo::cardano::dijkstra {
    account_balance_interval_t account_balance_interval_t::from_cbor(cbor::zero2::value &v)
    {
        if (v.type() == cbor::major_type::uint)
            return { v.uint() };
        auto &it = v.array();
        bounds_t bounds {};
        auto &lower = it.read();
        if (lower.is_null())
            static_cast<void>(lower.special());
        else
            bounds.lower = lower.uint();
        auto &upper = it.read();
        if (upper.is_null())
            static_cast<void>(upper.special());
        else
            bounds.upper = upper.uint();
        if (!it.done()) [[unlikely]]
            throw error("unexpected trailing account balance interval elements");
        if (!bounds.lower && !bounds.upper) [[unlikely]]
            throw error("both account balance interval bounds cannot be nil");
        return { std::move(bounds) };
    }

    direct_deposits_t direct_deposits_t::from_cbor(cbor::zero2::value &v)
    {
        direct_deposits_t res {};
        if (!v.indefinite()) [[likely]]
            res.reserve(v.special_uint());
        auto &it = v.map();
        while (!it.done()) {
            auto &key = it.read_key();
            reward_id_t reward { key.bytes() };
            auto &value = it.read_val(std::move(key));
            const auto before = res.size();
            res.emplace_hint(res.end(), std::move(reward), value.uint());
            if (res.size() == before) [[unlikely]]
                throw error("duplicate Dijkstra direct deposit reward account");
        }
        if (res.empty()) [[unlikely]]
            throw error("Dijkstra direct deposits must be nonempty when supplied");
        return res;
    }
}
