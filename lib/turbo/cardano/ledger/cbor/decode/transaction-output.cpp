/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/alonzo/cbor/decode/transaction-output.hpp>
#include <turbo/cardano/babbage/cbor/decode/transaction-output.hpp>
#include <turbo/cardano/conway/transaction.hpp>

namespace turbo::cardano {
    tx_out_data tx_out_data::from_cbor(cbor::zero2::value &v)
    {
        switch (const auto type = v.type(); type) {
            case cbor::major_type::array:
                return alonzo::detail::transaction_output_array_from_cbor(v);
            case cbor::major_type::map:
                return babbage::detail::transaction_output_map_from_cbor(
                    v,
                    [](auto &value) {
                        return output_value_t::from_cbor(value);
                    },
                    [](auto &script) {
                        return std::move(conway::script_t::from_cbor(script).value);
                    });
            default:
                throw error(fmt::format("unsupported cbor type in transaction_output: {}", type));
        }
    }
}
