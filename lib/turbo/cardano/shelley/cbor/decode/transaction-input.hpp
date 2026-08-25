#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/shelley/block.hpp>

namespace turbo::cardano::shelley::detail {
    inline tx_out_ref transaction_input_from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        tx_out_ref res { it.read().bytes(), it.read().uint() };
        if (!it.done()) [[unlikely]]
            throw error("unexpected trailing transaction_input elements");
        return res;
    }
}
