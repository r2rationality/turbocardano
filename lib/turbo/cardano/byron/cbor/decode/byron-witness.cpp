/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/byron/block.hpp>

namespace turbo::cardano {
    tx_wit_byron_vkey tx_wit_byron_vkey::from_cbor(cbor::zero2::value &v)
    {
        auto pv = cbor::zero2::parse(v.tag().read().bytes());
        auto &it = pv.get().array();
        return { it.read().bytes(), it.read().bytes() };
    }

    tx_wit_byron_redeemer tx_wit_byron_redeemer::from_cbor(cbor::zero2::value &v)
    {
        auto pv = cbor::zero2::parse(v.tag().read().bytes());
        auto &it = pv.get().array();
        return { it.read().bytes(), it.read().bytes() };
    }
}
