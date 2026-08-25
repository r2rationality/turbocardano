#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/shelley/cbor/decode/protocol-parameter.hpp>

namespace turbo::cardano::alonzo::detail {
    template<bool LEGACY_FIELDS, bool MIN_UTXO_VALUE, typename COST_MODELS_DECODER>
    bool protocol_param_update_from_cbor(
        param_update &upd,
        const uint64_t id,
        cbor::zero2::value &value,
        COST_MODELS_DECODER &&decode_cost_models)
    {
        if (shelley::detail::protocol_param_update_from_cbor<LEGACY_FIELDS, MIN_UTXO_VALUE>(upd, id, value))
            return true;
        switch (id) {
            case 17: upd.lovelace_per_utxo_byte.emplace_cbor(value); break;
            case 18: upd.plutus_cost_models.emplace(decode_cost_models(value)); break;
            case 19: upd.ex_unit_prices.emplace_cbor(value); break;
            case 20: upd.max_tx_ex_units.emplace_cbor(value); break;
            case 21: upd.max_block_ex_units.emplace_cbor(value); break;
            case 22: upd.max_value_size.emplace_cbor(value); break;
            case 23: upd.max_collateral_pct.emplace_cbor(value); break;
            case 24: upd.max_collateral_inputs.emplace_cbor(value); break;
            default: return false;
        }
        return true;
    }
}
