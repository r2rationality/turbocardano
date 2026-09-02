/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include "transaction-body.hpp"

namespace turbo::cardano::alonzo::full::detail {
    transaction_body_raw_fields_t transaction_body_raw_fields_from_cbor(const buffer raw)
    {
        transaction_body_raw_fields_t res {};
        if (raw.empty())
            return res;
        cbor::zero2::decoder dec { raw };
        auto &it = dec.read().map();
        while (!it.done()) {
            auto &key = it.read_key();
            const auto type = key.uint();
            auto &value = it.read_val(std::move(key));
            switch (type) {
                case 7: res.auxiliary_data_hash = hash_32 { value.bytes() }; break;
                case 11: res.script_data_hash = hash_32 { value.bytes() }; break;
                case 15: {
                    const auto network = value.uint();
                    if (network > 1) [[unlikely]]
                        throw error("network_id must be 0 or 1");
                    res.network_id = numeric_cast<uint8_t>(network);
                    break;
                }
                default: static_cast<void>(value.data_raw()); break;
            }
        }
        if (!dec.done()) [[unlikely]]
            throw error("transaction body contains trailing CBOR data");
        return res;
    }
}
