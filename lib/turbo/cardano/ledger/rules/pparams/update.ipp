/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.PParams.
// Included in turbo::cardano by common/types.cpp.

    template<typename TGT, typename SRC>
    void _apply_one_param_update(TGT &tgt, std::string &desc, const std::optional<SRC> &upd, const std::string_view name)
    {
        if (upd) {
            tgt = static_cast<TGT>(*upd);
            desc += fmt::format("{}: {} ", name, tgt);
        }
    }

    template<>
    void _apply_one_param_update(plutus_cost_models &tgt, std::string &desc, const std::optional<plutus_cost_models> &upd, const std::string_view name)
    {
        if (upd) {
            for (const auto &[id, model]: upd->items)
                tgt.items[id] = model;
            desc += fmt::format("{}: {} ", name, tgt);
        }
    }

    std::string protocol_params::apply(const param_update &upd)
    {
        std::string update_desc {};
        _apply_one_param_update(protocol_ver, update_desc, upd.protocol_ver, "protocol_ver");
        _apply_one_param_update(min_fee_a, update_desc, upd.min_fee_a, "min_fee_a");
        _apply_one_param_update(min_fee_b, update_desc, upd.min_fee_b, "min_fee_b");
        _apply_one_param_update(max_block_body_size, update_desc, upd.max_block_body_size, "max_block_body_size");
        _apply_one_param_update(max_transaction_size, update_desc, upd.max_transaction_size, "max_transaction_size");
        _apply_one_param_update(max_block_header_size, update_desc, upd.max_block_header_size, "max_block_header_size");
        _apply_one_param_update(key_deposit, update_desc, upd.key_deposit, "key_deposit");
        _apply_one_param_update(pool_deposit, update_desc, upd.pool_deposit, "pool_deposit");
        _apply_one_param_update(e_max, update_desc, upd.e_max, "e_max");
        _apply_one_param_update(n_opt, update_desc, upd.n_opt, "n_opt");
        _apply_one_param_update(pool_pledge_influence, update_desc, upd.pool_pledge_influence, "pool_pledge_influence");
        _apply_one_param_update(expansion_rate, update_desc, upd.expansion_rate, "expansion_rate");
        _apply_one_param_update(treasury_growth_rate, update_desc, upd.treasury_growth_rate, "treasury_growth_rate");
        _apply_one_param_update(decentralization, update_desc, upd.decentralization, "decentralization");
        _apply_one_param_update(extra_entropy, update_desc, upd.extra_entropy, "extra_entropy");
        _apply_one_param_update(min_utxo_value, update_desc, upd.min_utxo_value, "min_utxo_value");
        _apply_one_param_update(min_pool_cost, update_desc, upd.min_pool_cost, "min_pool_cost");
        _apply_one_param_update(lovelace_per_utxo_byte, update_desc, upd.lovelace_per_utxo_byte, "lovelace_per_utxo_byte");
        _apply_one_param_update(ex_unit_prices, update_desc, upd.ex_unit_prices, "ex_unit_prices");
        _apply_one_param_update(max_tx_ex_units, update_desc, upd.max_tx_ex_units, "max_tx_ex_units");
        _apply_one_param_update(max_block_ex_units, update_desc, upd.max_block_ex_units, "max_block_ex_units");
        _apply_one_param_update(max_value_size, update_desc, upd.max_value_size, "max_value_size");
        _apply_one_param_update(max_collateral_pct, update_desc, upd.max_collateral_pct, "max_collateral_pct");
        _apply_one_param_update(max_collateral_inputs, update_desc, upd.max_collateral_inputs, "max_collateral_inputs");
        _apply_one_param_update(plutus_cost_models, update_desc, upd.plutus_cost_models, "plutus_cost_models");
        return update_desc;
    }

    std::string protocol_params::apply(const param_update_t &upd)
    {
        std::string update_desc {};
        _apply_one_param_update(min_fee_a, update_desc, upd.min_fee_a, "min_fee_a");
        _apply_one_param_update(min_fee_b, update_desc, upd.min_fee_b, "min_fee_b");
        _apply_one_param_update(max_block_body_size, update_desc, upd.max_block_body_size, "max_block_body_size");
        _apply_one_param_update(max_transaction_size, update_desc, upd.max_transaction_size, "max_transaction_size");
        _apply_one_param_update(max_block_header_size, update_desc, upd.max_block_header_size, "max_block_header_size");
        _apply_one_param_update(key_deposit, update_desc, upd.key_deposit, "key_deposit");
        _apply_one_param_update(pool_deposit, update_desc, upd.pool_deposit, "pool_deposit");
        _apply_one_param_update(e_max, update_desc, upd.e_max, "e_max");
        _apply_one_param_update(n_opt, update_desc, upd.n_opt, "n_opt");
        _apply_one_param_update(pool_pledge_influence, update_desc, upd.pool_pledge_influence, "pool_pledge_influence");
        _apply_one_param_update(expansion_rate, update_desc, upd.expansion_rate, "expansion_rate");
        _apply_one_param_update(treasury_growth_rate, update_desc, upd.treasury_growth_rate, "treasury_growth_rate");
        _apply_one_param_update(min_pool_cost, update_desc, upd.min_pool_cost, "min_pool_cost");
        _apply_one_param_update(lovelace_per_utxo_byte, update_desc, upd.lovelace_per_utxo_byte, "lovelace_per_utxo_byte");
        _apply_one_param_update(plutus_cost_models, update_desc, upd.plutus_cost_models, "plutus_cost_models");
        _apply_one_param_update(ex_unit_prices, update_desc, upd.ex_unit_prices, "ex_unit_prices");
        _apply_one_param_update(max_tx_ex_units, update_desc, upd.max_tx_ex_units, "max_tx_ex_units");
        _apply_one_param_update(max_block_ex_units, update_desc, upd.max_block_ex_units, "max_block_ex_units");
        _apply_one_param_update(max_value_size, update_desc, upd.max_value_size, "max_value_size");
        _apply_one_param_update(max_collateral_pct, update_desc, upd.max_collateral_pct, "max_collateral_pct");
        _apply_one_param_update(max_collateral_inputs, update_desc, upd.max_collateral_inputs, "max_collateral_inputs");
        _apply_one_param_update(pool_voting_thresholds, update_desc, upd.pool_voting_thresholds, "pool_voting_thresholds");
        _apply_one_param_update(drep_voting_thresholds, update_desc, upd.drep_voting_thresholds, "drep_voting_thresholds");
        _apply_one_param_update(committee_min_size, update_desc, upd.committee_min_size, "committee_min_size");
        _apply_one_param_update(committee_max_term_length, update_desc, upd.committee_max_term_length, "committee_max_term_length");
        _apply_one_param_update(gov_action_lifetime, update_desc, upd.gov_action_lifetime, "gov_action_lifetime");
        _apply_one_param_update(gov_action_deposit, update_desc, upd.gov_action_deposit, "gov_action_deposit");
        _apply_one_param_update(drep_deposit, update_desc, upd.drep_deposit, "drep_deposit");
        _apply_one_param_update(drep_activity, update_desc, upd.drep_activity, "drep_activity");
        _apply_one_param_update(min_fee_ref_script_cost_per_byte, update_desc, upd.min_fee_ref_script_cost_per_byte, "min_fee_ref_script_cost_per_byte");
        _apply_one_param_update(max_ref_script_size_per_block, update_desc, upd.max_ref_script_size_per_block, "max_ref_script_size_per_block");
        _apply_one_param_update(max_ref_script_size_per_tx, update_desc, upd.max_ref_script_size_per_tx, "max_ref_script_size_per_tx");
        _apply_one_param_update(ref_script_cost_stride, update_desc, upd.ref_script_cost_stride, "ref_script_cost_stride");
        _apply_one_param_update(ref_script_cost_multiplier, update_desc, upd.ref_script_cost_multiplier, "ref_script_cost_multiplier");
        _apply_one_param_update(max_pledge_leverage, update_desc, upd.max_pledge_leverage, "max_pledge_leverage");
        _apply_one_param_update(min_pool_margin, update_desc, upd.min_pool_margin, "min_pool_margin");
        return update_desc;
    }

