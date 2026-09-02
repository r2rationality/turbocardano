/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/alonzo/block.hpp>
#include <turbo/cardano/alonzo/cbor/decode/protocol-parameter.hpp>

namespace turbo::cardano::alonzo {
    protocol_param_update_t protocol_param_update_t::from_cbor(cbor::zero2::value &v)
    {
        protocol_param_update_t res {};
        auto &it = v.map();
        while (!it.done()) {
            auto &id = it.read_key();
            const auto idx = id.uint();
            auto &value = it.read_val(std::move(id));
            if (!detail::protocol_param_update_from_cbor<true, false>(
                    res.value,
                    idx,
                    value,
                    [](auto &cost_models) {
                        return std::move(cost_models_t::from_cbor(cost_models).value);
                    })) [[unlikely]]
                throw error(fmt::format("unsupported Alonzo protocol parameter: {}", idx));
        }
        return res;
    }
}

namespace turbo::cardano {
    using namespace crypto;

    ex_unit_prices ex_unit_prices::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        ex_unit_prices res { decltype(mem)::from_cbor(it.read()), decltype(steps)::from_cbor(it.read()) };
        if (!it.done()) [[unlikely]]
            throw error("unexpected trailing ex_unit_prices elements");
        return res;
    }

    ex_units ex_units::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        ex_units res {
            it.read().uint(),
            it.read().uint()
        };
        if (res.mem > std::numeric_limits<int64_t>::max()
                || res.steps > std::numeric_limits<int64_t>::max()) [[unlikely]]
            throw error("execution units exceed max_int64");
        if (!it.done()) [[unlikely]]
            throw error("unexpected trailing ex_units elements");
        return res;
    }
}
