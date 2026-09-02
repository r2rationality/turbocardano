/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/transaction.hpp>

namespace turbo::cardano::dijkstra {
    namespace {
        rational_u64 interval_from_cbor(cbor::zero2::value &v, const bool unit, const bool positive_numerator)
        {
            auto res = rational_u64::from_cbor(v);
            if (!res.denominator || (positive_numerator && !res.numerator) || (unit && res.numerator > res.denominator)) [[unlikely]]
                throw error("invalid Dijkstra protocol parameter interval");
            return res;
        }
    }

    protocol_param_update_t protocol_param_update_t::from_cbor(cbor::zero2::value &v)
    {
        protocol_param_update_t res {};
        auto &upd = res.value;
        uint64_t seen_low = 0;
        auto &it = v.map();
        while (!it.done()) {
            auto &key = it.read_key();
            const auto type = key.uint();
            if (type > 39) [[unlikely]]
                throw error(fmt::format("unsupported Dijkstra protocol parameter key: {}", type));
            const auto mask = uint64_t { 1 } << type;
            if (seen_low & mask) [[unlikely]]
                throw error(fmt::format("duplicate Dijkstra protocol parameter key: {}", type));
            seen_low |= mask;
            auto &value = it.read_val(std::move(key));
            switch (type) {
                case 0: upd.min_fee_a = value.uint(); break;
                case 1: upd.min_fee_b = value.uint(); break;
                case 2: upd.max_block_body_size = numeric_cast<uint32_t>(value.uint()); break;
                case 3: upd.max_transaction_size = numeric_cast<uint32_t>(value.uint()); break;
                case 4: upd.max_block_header_size = numeric_cast<uint16_t>(value.uint()); break;
                case 5: upd.key_deposit = value.uint(); break;
                case 6: upd.pool_deposit = value.uint(); break;
                case 7: upd.e_max = numeric_cast<uint32_t>(value.uint()); break;
                case 8: upd.n_opt = numeric_cast<uint16_t>(value.uint()); break;
                case 9: upd.pool_pledge_influence = interval_from_cbor(value, false, false); break;
                case 10: upd.expansion_rate = interval_from_cbor(value, true, false); break;
                case 11: upd.treasury_growth_rate = interval_from_cbor(value, true, false); break;
                case 16: upd.min_pool_cost = value.uint(); break;
                case 17: upd.lovelace_per_utxo_byte = value.uint(); break;
                case 18: upd.plutus_cost_models = conway::cost_models_t::from_cbor(value).value; break;
                case 19: upd.ex_unit_prices = ex_unit_prices::from_cbor(value); break;
                case 20: upd.max_tx_ex_units = ex_units::from_cbor(value); break;
                case 21: upd.max_block_ex_units = ex_units::from_cbor(value); break;
                case 22: upd.max_value_size = numeric_cast<uint32_t>(value.uint()); break;
                case 23: upd.max_collateral_pct = numeric_cast<uint16_t>(value.uint()); break;
                case 24: upd.max_collateral_inputs = numeric_cast<uint16_t>(value.uint()); break;
                case 25: upd.pool_voting_thresholds = pool_voting_thresholds_t::from_cbor(value); break;
                case 26: upd.drep_voting_thresholds = drep_voting_thresholds_t::from_cbor(value); break;
                case 27: upd.committee_min_size = numeric_cast<uint16_t>(value.uint()); break;
                case 28: upd.committee_max_term_length = numeric_cast<uint32_t>(value.uint()); break;
                case 29: upd.gov_action_lifetime = numeric_cast<uint32_t>(value.uint()); break;
                case 30: upd.gov_action_deposit = value.uint(); break;
                case 31: upd.drep_deposit = value.uint(); break;
                case 32: upd.drep_activity = numeric_cast<uint32_t>(value.uint()); break;
                case 33: upd.min_fee_ref_script_cost_per_byte = interval_from_cbor(value, false, false); break;
                case 34: upd.max_ref_script_size_per_block = numeric_cast<uint32_t>(value.uint()); break;
                case 35: upd.max_ref_script_size_per_tx = numeric_cast<uint32_t>(value.uint()); break;
                case 36: {
                    const auto stride = numeric_cast<uint32_t>(value.uint());
                    if (!stride) [[unlikely]]
                        throw error("refScript cost stride must be positive");
                    upd.ref_script_cost_stride = stride;
                    break;
                }
                case 37: upd.ref_script_cost_multiplier = interval_from_cbor(value, false, true); break;
                case 38: {
                    nil_optional_t<rational_u64> leverage {};
                    if (value.is_null()) {
                        static_cast<void>(value.special());
                    } else {
                        leverage.emplace(interval_from_cbor(value, false, false));
                    }
                    upd.max_pledge_leverage.emplace(std::move(leverage));
                    break;
                }
                case 39: upd.min_pool_margin = interval_from_cbor(value, true, false); break;
                [[unlikely]] default:
                    throw error(fmt::format("unsupported Dijkstra protocol parameter key: {}", type));
            }
        }
        return res;
    }
}
