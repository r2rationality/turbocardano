/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/shelley/block.hpp>

namespace turbo::cardano {
    tx_wit_shelley_vkey tx_wit_shelley_vkey::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        tx_wit_shelley_vkey res { it.read().bytes(), it.read().bytes() };
        if (!it.done()) [[unlikely]]
            throw error("unexpected trailing Shelley vkey witness elements");
        return res;
    }

    tx_wit_shelley_bootstrap tx_wit_shelley_bootstrap::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        tx_wit_shelley_bootstrap res {
            it.read().bytes(), it.read().bytes(), it.read().bytes(), it.read().bytes()
        };
        if (!it.done()) [[unlikely]]
            throw error("unexpected trailing Shelley bootstrap witness elements");
        return res;
    }
}
