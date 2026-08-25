/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/byron/block.hpp>

namespace turbo::cardano::byron {
    transaction_witness_set_t transaction_witness_set_t::from_cbor(cbor::zero2::value &v)
    {
        transaction_witness_set_t res {};
        auto &it = v.array_sized();
        res.items.reserve(v.special_uint());
        while (!it.done()) {
            auto &w_it = it.read().array();
            switch (const auto typ = w_it.read().uint(); typ) {
                case 0: res.items.emplace_back(tx_wit_byron_vkey::from_cbor(w_it.read())); break;
                case 2: res.items.emplace_back(tx_wit_byron_redeemer::from_cbor(w_it.read())); break;
                [[unlikely]] default: throw error(fmt::format("unsupported byron witness type: {}", typ));
            }
        }
        res.raw = v.data_raw();
        return res;
    }

}
