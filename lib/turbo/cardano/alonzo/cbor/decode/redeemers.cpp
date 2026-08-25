/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/alonzo/block.hpp>

namespace turbo::cardano::alonzo {
    void redeemers_t::add(tx_redeemer &&redeemer)
    {
        const auto id = redeemer.id();
        if (items.empty() || items.rbegin()->first < id) {
            items.emplace_hint(items.end(), id, std::move(redeemer));
            return;
        }
        const auto it = items.lower_bound(id);
        if (it != items.end() && !(id < it->first))
            it->second = std::move(redeemer);
        else
            items.emplace_hint(it, id, std::move(redeemer));
    }

    redeemers_t redeemers_t::from_cbor(cbor::zero2::value &v)
    {
        redeemers_t res {};
        auto &it = v.array();
        if (!v.indefinite()) [[likely]]
            res.items.reserve(v.special_uint());
        while (!it.done())
            res.add(std::move(redeemer_t::from_cbor(it.read()).value));
        res.raw = v.data_raw();
        return res;
    }
}
