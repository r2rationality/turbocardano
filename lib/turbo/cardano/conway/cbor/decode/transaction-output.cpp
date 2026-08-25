/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/conway/transaction.hpp>
#include <turbo/cardano/babbage/cbor/decode/transaction-output.hpp>

namespace turbo::cardano::conway {
    namespace {
        transaction_output_t from_array(cbor::zero2::value &v)
        {
            auto &it = v.array();
            auto address = it.read().bytes();
            auto value = value_t::from_cbor(it.read()).value;
            std::optional<datum_option_t> datum {};
            if (!it.done())
                datum.emplace(datum_hash { it.read().bytes() });
            if (!it.done()) [[unlikely]]
                throw error("unexpected trailing transaction_output elements");
            return {{ std::move(address), value.coin, std::move(value.assets), std::move(datum) }};
        }
    }

    transaction_output_t transaction_output_t::from_cbor(cbor::zero2::value &v)
    {
        if (v.type() == cbor::major_type::array)
            return from_array(v);
        return { babbage::detail::transaction_output_map_from_cbor(
            v,
            [](auto &value) {
                return std::move(value_t::from_cbor(value).value);
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
