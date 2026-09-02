/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/conway/block.hpp>

namespace turbo::cardano {
    bool param_update_t::security_group() const
    {
        bool res = false;
        res |= min_fee_a.has_value();
        res |= min_fee_b.has_value();
        res |= max_block_body_size.has_value();
        res |= max_transaction_size.has_value();
        res |= max_block_header_size.has_value();
        res |= lovelace_per_utxo_byte.has_value();
        res |= max_block_ex_units.has_value();
        res |= max_value_size.has_value();
        res |= gov_action_deposit.has_value();
        res |= min_fee_ref_script_cost_per_byte.has_value();
        res |= max_ref_script_size_per_block.has_value();
        res |= max_ref_script_size_per_tx.has_value();
        res |= ref_script_cost_stride.has_value();
        res |= ref_script_cost_multiplier.has_value();
        return res;
    }

    bool param_update_t::network_group() const
    {
        bool res = false;
        res |= max_block_body_size.has_value();
        res |= max_transaction_size.has_value();
        res |= max_block_header_size.has_value();
        res |= max_tx_ex_units.has_value();
        res |= max_block_ex_units.has_value();
        res |= max_value_size.has_value();
        res |= max_collateral_inputs.has_value();
        res |= max_ref_script_size_per_block.has_value();
        res |= max_ref_script_size_per_tx.has_value();
        res |= ref_script_cost_stride.has_value();
        res |= ref_script_cost_multiplier.has_value();
        return res;
    }

    bool param_update_t::economic_group() const
    {
        bool res = false;
        res |= min_fee_a.has_value();
        res |= min_fee_b.has_value();
        res |= key_deposit.has_value();
        res |= pool_deposit.has_value();
        res |= lovelace_per_utxo_byte.has_value();
        res |= ex_unit_prices.has_value();
        res |= min_fee_ref_script_cost_per_byte.has_value();
        res |= min_pool_margin.has_value();
        return res;
    }

    bool param_update_t::technical_group() const
    {
        bool res = false;
        res |= e_max.has_value();
        res |= n_opt.has_value();
        res |= expansion_rate.has_value();
        res |= max_collateral_pct.has_value();
        res |= plutus_cost_models.has_value();
        res |= max_pledge_leverage.has_value();
        return res;
    }

    bool param_update_t::governance_group() const
    {
        bool res = false;
        res |= pool_voting_thresholds.has_value();
        res |= drep_voting_thresholds.has_value();
        res |= committee_min_size.has_value();
        res |= committee_max_term_length.has_value();
        res |= gov_action_lifetime.has_value();
        res |= gov_action_deposit.has_value();
        res |= drep_deposit.has_value();
        res |= drep_activity.has_value();
        return res;
    }
}
