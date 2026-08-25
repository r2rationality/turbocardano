/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/shelley/block.hpp>

namespace turbo::cardano::shelley {
    withdrawals_t withdrawals_t::from_cbor(cbor::zero2::value &v)
    {
        withdrawals_t res {};
        if (!v.indefinite())
            res.reserve(v.special_uint());
        auto &it = v.map();
        while (!it.done()) {
            auto &k = it.read_key();
            auto reward_id = reward_id_t::from_cbor(k);
            res.emplace_hint(res.end(), std::move(reward_id), it.read_val(std::move(k)).uint());
        }
        return res;
    }
}
