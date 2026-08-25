#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/alonzo/block.hpp>

namespace turbo::cardano::alonzo::detail {
    inline tx_out_data transaction_output_array_from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        auto address = it.read().bytes();
        auto value = output_value_t::from_cbor(it.read());
        std::optional<datum_option_t> datum {};
        if (!it.done())
            datum.emplace(datum_hash { it.read().bytes() });
        if (!it.done()) [[unlikely]]
            throw error("unexpected trailing transaction_output elements");
        return { std::move(address), value.coin, std::move(value.assets), std::move(datum) };
    }
}
