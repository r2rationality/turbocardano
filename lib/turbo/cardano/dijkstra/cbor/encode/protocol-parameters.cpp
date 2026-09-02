/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/transaction.hpp>

namespace turbo::cardano::dijkstra {
    namespace {
        template<typename T>
        size_t encode_parameter(era_encoder &enc, const uint64_t key, const std::optional<T> &value)
        {
            if (!value)
                return 0;
            enc.uint(key);
            if constexpr (std::integral<T>)
                enc.uint(*value);
            else
                value->to_cbor(enc);
            return 1;
        }
    }

    void protocol_param_update_t::to_cbor(era_encoder &enc) const
    {
        auto values { enc };
        size_t count = 0;
        count += encode_parameter(values, 0, value.min_fee_a);
        count += encode_parameter(values, 1, value.min_fee_b);
        count += encode_parameter(values, 2, value.max_block_body_size);
        count += encode_parameter(values, 3, value.max_transaction_size);
        count += encode_parameter(values, 4, value.max_block_header_size);
        count += encode_parameter(values, 5, value.key_deposit);
        count += encode_parameter(values, 6, value.pool_deposit);
        count += encode_parameter(values, 7, value.e_max);
        count += encode_parameter(values, 8, value.n_opt);
        count += encode_parameter(values, 9, value.pool_pledge_influence);
        count += encode_parameter(values, 10, value.expansion_rate);
        count += encode_parameter(values, 11, value.treasury_growth_rate);
        count += encode_parameter(values, 16, value.min_pool_cost);
        count += encode_parameter(values, 17, value.lovelace_per_utxo_byte);
        count += encode_parameter(values, 18, value.plutus_cost_models);
        count += encode_parameter(values, 19, value.ex_unit_prices);
        count += encode_parameter(values, 20, value.max_tx_ex_units);
        count += encode_parameter(values, 21, value.max_block_ex_units);
        count += encode_parameter(values, 22, value.max_value_size);
        count += encode_parameter(values, 23, value.max_collateral_pct);
        count += encode_parameter(values, 24, value.max_collateral_inputs);
        count += encode_parameter(values, 25, value.pool_voting_thresholds);
        count += encode_parameter(values, 26, value.drep_voting_thresholds);
        count += encode_parameter(values, 27, value.committee_min_size);
        count += encode_parameter(values, 28, value.committee_max_term_length);
        count += encode_parameter(values, 29, value.gov_action_lifetime);
        count += encode_parameter(values, 30, value.gov_action_deposit);
        count += encode_parameter(values, 31, value.drep_deposit);
        count += encode_parameter(values, 32, value.drep_activity);
        count += encode_parameter(values, 33, value.min_fee_ref_script_cost_per_byte);
        count += encode_parameter(values, 34, value.max_ref_script_size_per_block);
        count += encode_parameter(values, 35, value.max_ref_script_size_per_tx);
        count += encode_parameter(values, 36, value.ref_script_cost_stride);
        count += encode_parameter(values, 37, value.ref_script_cost_multiplier);
        count += encode_parameter(values, 38, value.max_pledge_leverage);
        count += encode_parameter(values, 39, value.min_pool_margin);
        enc.map_compact(count, [&] { enc << values; });
    }
}
