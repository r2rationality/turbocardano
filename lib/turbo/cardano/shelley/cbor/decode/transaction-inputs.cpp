/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/shelley/block.hpp>

namespace turbo::cardano::shelley {
    transaction_inputs_t transaction_inputs_t::from_cbor(cbor::zero2::value &v)
    {
        transaction_inputs_t res {};
        if (!v.indefinite()) [[likely]]
            res.reserve(v.special_uint());
        auto &it = v.array();
        while (!it.done())
            res.emplace_hint(res.end(), std::move(transaction_input_t::from_cbor(it.read()).value));
        return res;
    }
}
