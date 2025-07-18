/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cardano/alonzo/block.hpp>
#include <turbo/cardano/ledger/alonzo.hpp>

namespace turbo::cardano::ledger::alonzo {
    vrf_state::vrf_state(shelley::vrf_state &&o): shelley::vrf_state { std::move(o) }
    {
        logger::debug("alonzo::vrf_state created max_epoch_slot: {}", _max_epoch_slot);
    }

    state::state(shelley::state &&o): shelley::state { std::move(o) }
    {
        _apply_alonzo_params(_params);
        _apply_alonzo_params(_params_prev);
    }

    void state::_params_to_cbor(era_encoder &enc, const protocol_params &params) const
    {
        enc.array(25);
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
        params.decentralization.to_cbor(enc);
        if (!params.extra_entropy)
            enc.array(1).uint(0);
        else
            enc.array(2).uint(1).bytes(*params.extra_entropy);
        enc.uint(params.protocol_ver.major);
        enc.uint(params.protocol_ver.minor);
        enc.uint(params.min_pool_cost);
        enc.uint(params.lovelace_per_utxo_byte);
        params.plutus_cost_models.to_cbor(enc);
        params.ex_unit_prices.to_cbor(enc);
        params.max_tx_ex_units.to_cbor(enc);
        params.max_block_ex_units.to_cbor(enc);
        enc.uint(params.max_value_size);
        enc.uint(params.max_collateral_pct);
        enc.uint(params.max_collateral_inputs);
    }

    void state::_apply_alonzo_params(protocol_params &p) const
    {
        const auto &al_cfg = _cfg.alonzo_genesis;
        p.lovelace_per_utxo_byte = json::value_to<uint64_t>(al_cfg.at("lovelacePerUTxOWord"));
        p.ex_unit_prices = decltype(p.ex_unit_prices)::from_json(al_cfg.at("executionPrices"));
        p.max_tx_ex_units = decltype(p.max_tx_ex_units)::from_json(al_cfg.at("maxTxExUnits"));
        p.max_block_ex_units = decltype(p.max_block_ex_units)::from_json(al_cfg.at("maxBlockExUnits"));
        p.max_value_size = json::value_to<uint64_t>(al_cfg.at("maxValueSize"));
        p.max_collateral_pct = json::value_to<uint64_t>(al_cfg.at("collateralPercentage"));
        p.max_collateral_inputs = json::value_to<uint64_t>(al_cfg.at("maxCollateralInputs"));
        p.plutus_cost_models.v1.emplace(plutus_cost_model::from_json(_cfg.plutus_all_cost_models.v1.value(), al_cfg.at("costModels").at("PlutusV1")));
    }

    void state::_apply_param_update(const param_update &update)
    {
        const auto update_desc = _params.apply(update);
        logger::info("epoch: {} protocol params update: [ {}]", _epoch, update_desc);
    }

    void state::_parse_protocol_params(protocol_params &params, cbor::zero2::value &val) const
    {
        _apply_shelley_params(params);
        _apply_alonzo_params(params);
        auto &it = val.array();
        params.min_fee_a = it.read().uint();
        params.min_fee_b = it.read().uint();
        params.max_block_body_size = it.read().uint();
        params.max_transaction_size = it.read().uint();
        params.max_block_header_size = it.read().uint();
        params.key_deposit = it.read().uint();
        params.pool_deposit = it.read().uint();
        params.e_max = it.read().uint();
        params.n_opt = it.read().uint();
        params.pool_pledge_influence = decltype(params.pool_pledge_influence)::from_cbor(it.read());
        params.expansion_rate = decltype(params.pool_pledge_influence)::from_cbor(it.read());
        params.treasury_growth_rate = decltype(params.treasury_growth_rate)::from_cbor(it.read());
        params.decentralization = decltype(params.decentralization)::from_cbor(it.read());
        params.extra_entropy = decltype(params.extra_entropy)::from_cbor(it.read());
        params.protocol_ver.major = it.read().uint();
        params.protocol_ver.minor = it.read().uint();
        params.min_pool_cost = it.read().uint();
        params.lovelace_per_utxo_byte = it.read().uint();
        params.plutus_cost_models = decltype(params.plutus_cost_models)::from_cbor(it.read());
        params.ex_unit_prices = decltype(params.ex_unit_prices)::from_cbor(it.read());
        params.max_tx_ex_units = decltype(params.max_tx_ex_units)::from_cbor(it.read());
        params.max_block_ex_units = decltype(params.max_block_ex_units)::from_cbor(it.read());
        params.max_value_size = it.read().uint();
        params.max_collateral_pct = it.read().uint();
        params.max_collateral_inputs = it.read().uint();
    }

    void state::_param_update_to_cbor(era_encoder &enc, const param_update &upd) const
    {
        auto my_enc { enc };
        size_t cnt = _param_update_common_to_cbor(my_enc, upd);
        cnt += _param_to_cbor(my_enc, 16, upd.min_pool_cost);
        cnt += _param_to_cbor(my_enc, 17, upd.lovelace_per_utxo_byte);
        if (upd.plutus_cost_models) {
            ++cnt;
            my_enc.uint(18);
            upd.plutus_cost_models->to_cbor(my_enc);
        }
        if (upd.ex_unit_prices) {
            ++cnt;
            my_enc.uint(19);
            upd.ex_unit_prices->to_cbor(my_enc);
        }
        if (upd.max_tx_ex_units) {
            ++cnt;
            my_enc.uint(20);
            my_enc.array(2).uint(upd.max_tx_ex_units->mem).uint(upd.max_tx_ex_units->steps);
        }
        if (upd.max_block_ex_units) {
            ++cnt;
            my_enc.uint(21);
            my_enc.array(2).uint(upd.max_block_ex_units->mem).uint(upd.max_block_ex_units->steps);
        }
        cnt += _param_to_cbor(my_enc, 22, upd.max_value_size);
        cnt += _param_to_cbor(my_enc, 23, upd.max_collateral_pct);
        cnt += _param_to_cbor(my_enc, 24, upd.max_collateral_inputs);
        enc.map(cnt);
        enc << my_enc;
    }
}
