/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/babbage/cbor/decode/transaction-output.hpp>
#include <turbo/cardano/alonzo/cbor/decode/transaction-output.hpp>

namespace turbo::cardano::babbage {
    transaction_output_t transaction_output_t::from_cbor(cbor::zero2::value &v)
    {
        if (v.type() == cbor::major_type::array)
            return { alonzo::detail::transaction_output_array_from_cbor(v) };
        return { detail::transaction_output_map_from_cbor(
            v,
            [](auto &value) {
                return output_value_t::from_cbor(value);
            },
            [](auto &script) {
                return std::move(script_t::from_cbor(script).value);
            }) };
    }

    transaction_outputs_t transaction_outputs_t::from_cbor(cbor::zero2::value &v)
    {
        transaction_outputs_t res {};
        if (!v.indefinite()) [[likely]]
            res.value.reserve(v.special_uint());
        auto &it = v.array();
        while (!it.done())
            res.value.emplace_back(std::move(transaction_output_t::from_cbor(it.read()).value));
        return res;
    }
}
