/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

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

    void state::_apply_alonzo_params(protocol_params &p) const
    {
        const auto &initial = _cfg.alonzo_protocol_params;
        p.lovelace_per_utxo_byte = initial.lovelace_per_utxo_byte;
        p.ex_unit_prices = initial.ex_unit_prices;
        p.max_tx_ex_units = initial.max_tx_ex_units;
        p.max_block_ex_units = initial.max_block_ex_units;
        p.max_value_size = initial.max_value_size;
        p.max_collateral_pct = initial.max_collateral_pct;
        p.max_collateral_inputs = initial.max_collateral_inputs;
        p.plutus_cost_models.items.emplace(0, initial.plutus_cost_models.at(0));
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

}
