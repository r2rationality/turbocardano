/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/ledger/babbage.hpp>

namespace turbo::cardano::ledger::babbage {
    vrf_state::vrf_state(alonzo::vrf_state &&o): alonzo::vrf_state { std::move(o) }
    {
        logger::debug("babbage::vrf_state created max_epoch_slot: {}", _max_epoch_slot);
    }

    void vrf_state::from_cbor(cbor::zero2::value &v)
    {
        auto &vit = v.array();
        auto &raw = vit.skip(1).read();
        auto &rit = raw.array();
        _slot_last = rit.read().array().skip(1).read().uint();
        _kes_counters = decltype(_kes_counters)::from_cbor(rit.read());
        _nonce_evolving = rit.read().at(1).bytes();
        _nonce_candidate =rit.read().at(1).bytes();
        _nonce_epoch = rit.read().at(1).bytes();
        _lab_prev_hash = rit.read().at(1).bytes();
        _prev_epoch_lab_prev_hash = decltype(_prev_epoch_lab_prev_hash)::from_cbor(rit.read());
    }

    state::state(alonzo::state &&o): alonzo::state { std::move(o) }
    {
        _apply_babbage_params(_params);
        _apply_babbage_params(_params_prev);
    }

    void state::_apply_babbage_params(protocol_params &p) const
    {
        p.decentralization = rational_u64 { 0, 1 };
        p.lovelace_per_utxo_byte = 4310;
    }

    void state::_apply_param_update(const param_update &update)
    {
        std::string update_desc = _params.apply(update);
        logger::info("epoch: {} protocol params update: [ {}]", _epoch, update_desc);
    }

    void state::_parse_protocol_params(protocol_params &params, cbor::zero2::value &v) const
    {
        _apply_shelley_params(params);
        _apply_alonzo_params(params);
        _apply_babbage_params(params);
        auto &it = v.array();
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
        params.expansion_rate = decltype(params.expansion_rate)::from_cbor(it.read());
        params.treasury_growth_rate = decltype(params.treasury_growth_rate)::from_cbor(it.read());
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
