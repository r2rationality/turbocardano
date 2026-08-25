#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/types.hpp>

namespace turbo::cardano::shelley::detail {
    template<bool LEGACY_FIELDS, bool MIN_UTXO_VALUE>
    bool protocol_param_update_from_cbor(param_update &upd, const uint64_t id, cbor::zero2::value &value)
    {
        switch (id) {
            case 0: upd.min_fee_a.emplace_cbor(value); break;
            case 1: upd.min_fee_b.emplace_cbor(value); break;
            case 2: upd.max_block_body_size.emplace_cbor(value); break;
            case 3: upd.max_transaction_size.emplace_cbor(value); break;
            case 4: upd.max_block_header_size.emplace_cbor(value); break;
            case 5: upd.key_deposit.emplace_cbor(value); break;
            case 6: upd.pool_deposit.emplace_cbor(value); break;
            case 7: upd.e_max.emplace_cbor(value); break;
            case 8: upd.n_opt.emplace_cbor(value); break;
            case 9: upd.pool_pledge_influence.emplace_cbor(value); break;
            case 10: upd.expansion_rate.emplace_cbor(value); break;
            case 11: upd.treasury_growth_rate.emplace_cbor(value); break;
            case 12:
                if constexpr (LEGACY_FIELDS) {
                    upd.decentralization.emplace_cbor(value);
                    break;
                } else {
                    return false;
                }
            case 13:
                if constexpr (LEGACY_FIELDS) {
                    upd.extra_entropy.emplace_cbor(value);
                    break;
                } else {
                    return false;
                }
            case 14: upd.protocol_ver.emplace_cbor(value); break;
            case 15:
                if constexpr (MIN_UTXO_VALUE) {
                    upd.min_utxo_value.emplace_cbor(value);
                    break;
                } else {
                    return false;
                }
            case 16: upd.min_pool_cost.emplace_cbor(value); break;
            default: return false;
        }
        return true;
    }
}
