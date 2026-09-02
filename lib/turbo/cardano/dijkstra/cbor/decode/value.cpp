/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/transaction.hpp>

namespace turbo::cardano::dijkstra {
    value_t value_t::from_cbor(cbor::zero2::value &v)
    {
        const bool has_multiasset = v.type() == cbor::major_type::array;
        auto decoded = conway::value_t::from_cbor(v);
        if (has_multiasset && decoded.value.assets.empty()) [[unlikely]]
            throw error("a Dijkstra multiasset value must contain at least one policy");
        return { std::move(decoded.value) };
    }
}
