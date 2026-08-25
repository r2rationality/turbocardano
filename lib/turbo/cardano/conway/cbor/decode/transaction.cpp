/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/conway/transaction.hpp>

namespace turbo::cardano::conway {
    transaction_t transaction_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        transaction_t res {
            transaction_body_t::from_cbor(it.read()),
            transaction_witness_set_t::from_cbor(it.read()),
            it.read().boolean()
        };
        auto &auxiliary_data = it.read();
        if (!auxiliary_data.is_null())
            res.auxiliary_data.emplace(auxiliary_data_t::from_cbor(auxiliary_data));
        return res;
    }
}
