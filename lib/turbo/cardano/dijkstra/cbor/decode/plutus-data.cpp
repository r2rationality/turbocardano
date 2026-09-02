/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/transaction.hpp>
#include <turbo/plutus/types.hpp>

namespace turbo::cardano::dijkstra {
    plutus_data_t plutus_data_t::from_cbor(cbor::zero2::value &v)
    {
        plutus::data::validate_cbor(v);
        const auto raw = v.data_raw();
        return { uint8_vector { raw.begin(), raw.end() } };
    }
}
