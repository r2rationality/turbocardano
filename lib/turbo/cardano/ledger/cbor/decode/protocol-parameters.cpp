/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/alonzo/cbor/decode/protocol-parameter.hpp>

namespace turbo::cardano {
    param_update param_update::from_cbor(cbor::zero2::value &v)
    {
        param_update res {};
        auto &it = v.map();
        while (!it.done()) {
            auto &key = it.read_key();
            const auto id = key.uint();
            auto &value = it.read_val(std::move(key));
            if (!alonzo::detail::protocol_param_update_from_cbor<true, true>(
                    res,
                    id,
                    value,
                    [](auto &cost_models) {
                        return plutus_cost_models::from_cbor(cost_models);
                    })) [[unlikely]]
                throw error(fmt::format("unsupported ledger protocol parameter: {}", id));
        }
        return res;
    }
}
