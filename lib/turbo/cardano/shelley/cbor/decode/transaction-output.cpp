/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/shelley/block.hpp>

namespace turbo::cardano::shelley {
    transaction_output_t transaction_output_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        transaction_output_t res {{ it.read().bytes(), it.read().uint() }};
        if (!it.done()) [[unlikely]]
            throw error("unexpected trailing transaction_output elements");
        return res;
    }
}
