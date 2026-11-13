/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/ledger/dijkstra.hpp>

namespace turbo::cardano::ledger::dijkstra {
    state::state(conway::state &&o): conway::state { std::move(o) }
    {
        if (!_cfg.dijkstra_genesis) [[unlikely]]
            throw error{"Dijkstra transition requires a verified Dijkstra genesis"};
        conway::state::complete_pulsers();
        for (auto *params: { &_params, &_params_prev, &_enact_state.params, &_enact_state.prev_params,
                &_ratify_state.new_state.params, &_ratify_state.new_state.prev_params })
            _apply_genesis(*params);
        _populate_pool_vrf_key_hashes();
    }

    void state::_apply_genesis(protocol_params &p) const
    {
        const auto &g = *_cfg.dijkstra_genesis;
        p.max_ref_script_size_per_block = g.max_ref_script_size_per_block;
        p.max_ref_script_size_per_tx = g.max_ref_script_size_per_tx;
        p.ref_script_cost_stride = g.ref_script_cost_stride;
        p.ref_script_cost_multiplier = g.ref_script_cost_multiplier;
        p.max_pledge_leverage.reset();
        if (g.max_pledge_leverage)
            p.max_pledge_leverage.emplace(*g.max_pledge_leverage);
        p.min_pool_margin = g.min_pool_margin;
        p.leios_announcement_period_length = g.leios_announcement_period_length;
        p.leios_vote_period_length = g.leios_vote_period_length;
        p.leios_diffusion_period_length = g.leios_diffusion_period_length;
        p.leios_committee_size = g.leios_committee_size;
        p.leios_quorum_stake_threshold = g.leios_quorum_stake_threshold;
        p.max_endorser_block_references_size = g.max_endorser_block_references_size;
        p.max_endorser_block_txs_size = g.max_endorser_block_txs_size;
        p.max_endorser_block_ex_units = g.max_endorser_block_execution_units;
        p.max_ref_script_size_per_endorser_block = g.max_ref_script_size_per_endorser_block;
        p.peras_min_candidate_block_age = g.peras_min_candidate_block_age;
        p.peras_healing_factor = g.peras_healing_factor;
        p.peras_cert_boost = g.peras_cert_boost;
        p.peras_target_committee_size = g.peras_target_committee_size;
        p.peras_bootstrap_round = g.peras_bootstrap_round;
        p.peras_quorum_threshold_safety_margin = g.peras_quorum_threshold_safety_margin;
        p.ref_inputs_cost_per_multi_asset_policy = g.ref_inputs_cost_per_multi_asset_policy;
        p.ref_inputs_cost_per_datum_byte = g.ref_inputs_cost_per_datum_byte;
        p.plutus_cost_models.items.insert_or_assign(3, plutus_cost_model { g.plutus_v4_cost_model });
    }

    point state::from_cbor(cbor::zero2::value &)
    {
        throw error{"Dijkstra node snapshot import is not supported; use an internal checkpoint or replay"};
    }

    void state::to_cbor(cbor_encoder &, node_format) const
    {
        throw error{"Dijkstra node snapshot export is not supported"};
    }
}
