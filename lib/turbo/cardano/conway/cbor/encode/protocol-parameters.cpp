/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/conway/block.hpp>

namespace turbo::cardano::conway {
    void protocol_params_to_cbor(era_encoder &enc, const protocol_params &params)
    {
        enc.array(31);
        enc.uint(params.min_fee_a);
        enc.uint(params.min_fee_b);
        enc.uint(params.max_block_body_size);
        enc.uint(params.max_transaction_size);
        enc.uint(params.max_block_header_size);
        enc.uint(params.key_deposit);
        enc.uint(params.pool_deposit);
        enc.uint(params.e_max);
        enc.uint(params.n_opt);
        params.pool_pledge_influence.to_cbor(enc);
        params.expansion_rate.to_cbor(enc);
        params.treasury_growth_rate.to_cbor(enc);
        enc.array(2)
            .uint(params.protocol_ver.major)
            .uint(params.protocol_ver.minor);
        enc.uint(params.min_pool_cost);
        enc.uint(params.lovelace_per_utxo_byte);
        params.plutus_cost_models.to_cbor(enc);
        params.ex_unit_prices.to_cbor(enc);
        params.max_tx_ex_units.to_cbor(enc);
        params.max_block_ex_units.to_cbor(enc);
        enc.uint(params.max_value_size);
        enc.uint(params.max_collateral_pct);
        enc.uint(params.max_collateral_inputs);
        params.pool_voting_thresholds.to_cbor(enc);
        params.drep_voting_thresholds.to_cbor(enc);
        enc.uint(params.committee_min_size);
        enc.uint(params.committee_max_term_length);
        enc.uint(params.gov_action_lifetime);
        enc.uint(params.gov_action_deposit);
        enc.uint(params.drep_deposit);
        enc.uint(params.drep_activity);
        params.min_fee_ref_script_cost_per_byte.to_cbor(enc);
    }
}

namespace turbo::cardano {
    template<std::integral T>
    size_t param_encode(era_encoder &enc, const size_t id, const std::optional<T> &val)
    {
        if (val) {
            enc.uint(id);
            enc.uint(*val);
            return 1;
        }
        return 0;
    }

    template<typename T>
    size_t param_encode(era_encoder &enc, const size_t id, const std::optional<T> &val)
    {
        if (val) {
            enc.uint(id);
            val->to_cbor(enc);
            return 1;
        }
        return 0;
    }

    void param_update_t::to_cbor(era_encoder &enc) const
    {
        auto l_enc { enc };
        size_t cnt = 0;
        cnt += param_encode(l_enc, 0, min_fee_a);
        cnt += param_encode(l_enc, 1, min_fee_b);
        cnt += param_encode(l_enc, 2, max_block_body_size);
        cnt += param_encode(l_enc, 3, max_transaction_size);
        cnt += param_encode(l_enc, 4, max_block_header_size);
        cnt += param_encode(l_enc, 5, key_deposit);
        cnt += param_encode(l_enc, 6, pool_deposit);
        cnt += param_encode(l_enc, 7, e_max);
        cnt += param_encode(l_enc, 8, n_opt);
        cnt += param_encode(l_enc, 9, pool_pledge_influence);
        cnt += param_encode(l_enc, 10, expansion_rate);
        cnt += param_encode(l_enc, 11, treasury_growth_rate);
        cnt += param_encode(l_enc, 16, min_pool_cost);
        cnt += param_encode(l_enc, 17, lovelace_per_utxo_byte);
        cnt += param_encode(l_enc, 18, plutus_cost_models);
        cnt += param_encode(l_enc, 19, ex_unit_prices);
        cnt += param_encode(l_enc, 20, max_tx_ex_units);
        cnt += param_encode(l_enc, 21, max_block_ex_units);
        cnt += param_encode(l_enc, 22, max_value_size);
        cnt += param_encode(l_enc, 23, max_collateral_pct);
        cnt += param_encode(l_enc, 24, max_collateral_inputs);
        cnt += param_encode(l_enc, 25, pool_voting_thresholds);
        cnt += param_encode(l_enc, 26, drep_voting_thresholds);
        cnt += param_encode(l_enc, 27, committee_min_size);
        cnt += param_encode(l_enc, 28, committee_max_term_length);
        cnt += param_encode(l_enc, 29, gov_action_lifetime);
        cnt += param_encode(l_enc, 30, gov_action_deposit);
        cnt += param_encode(l_enc, 31, drep_deposit);
        cnt += param_encode(l_enc, 32, drep_activity);
        cnt += param_encode(l_enc, 33, min_fee_ref_script_cost_per_byte);
        cnt += param_encode(l_enc, 34, max_ref_script_size_per_block);
        cnt += param_encode(l_enc, 35, max_ref_script_size_per_tx);
        cnt += param_encode(l_enc, 36, ref_script_cost_stride);
        cnt += param_encode(l_enc, 37, ref_script_cost_multiplier);
        cnt += param_encode(l_enc, 38, max_pledge_leverage);
        cnt += param_encode(l_enc, 39, min_pool_margin);
        if (enc.era() == era_t::dijkstra) {
            cnt += param_encode(l_enc, 40, leios_announcement_period_length);
            cnt += param_encode(l_enc, 41, leios_vote_period_length);
            cnt += param_encode(l_enc, 42, leios_diffusion_period_length);
            cnt += param_encode(l_enc, 43, leios_committee_size);
            cnt += param_encode(l_enc, 44, leios_quorum_stake_threshold);
            cnt += param_encode(l_enc, 45, max_endorser_block_references_size);
            cnt += param_encode(l_enc, 46, max_endorser_block_txs_size);
            cnt += param_encode(l_enc, 47, max_endorser_block_ex_units);
            cnt += param_encode(l_enc, 48, max_ref_script_size_per_endorser_block);
            cnt += param_encode(l_enc, 49, peras_min_candidate_block_age);
            cnt += param_encode(l_enc, 50, peras_healing_factor);
            cnt += param_encode(l_enc, 51, peras_cert_boost);
            cnt += param_encode(l_enc, 52, peras_target_committee_size);
            cnt += param_encode(l_enc, 53, peras_bootstrap_round);
            cnt += param_encode(l_enc, 54, peras_quorum_threshold_safety_margin);
            cnt += param_encode(l_enc, 55, ref_inputs_cost_per_multi_asset_policy);
            cnt += param_encode(l_enc, 56, ref_inputs_cost_per_datum_byte);
        }
        enc.map_compact(cnt, [&] {
            enc << l_enc;
        });
    }
}
