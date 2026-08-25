#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/ledger/conway.hpp>

namespace turbo::cardano::ledger::conway::detail {
    template<typename M, typename K>
    auto map_nice_at(const M &map, const K &key) -> decltype(map.at(key))
    {
        const auto it = map.find(key);
        if (it == map.end()) [[unlikely]]
            throw error(fmt::format("unable to find key {} in map of type {}", key, typeid(M).name()));
        return it->second;
    }

    template<typename M, typename K>
    auto map_nice_at(M &map, const K &key) -> decltype(map.at(key))
    {
        return const_cast<decltype(map.at(key))>(map_nice_at(const_cast<const M &>(map), key));
    }
}
