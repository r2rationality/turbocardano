/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <algorithm>
#include <turbo/cardano/ledger/conway.hpp>

namespace turbo::cardano::ledger::conway {
    committee_t committee_t::from_json(const json::value &j)
    {
        member_map members {};
        const auto &json_members = j.at("members").as_object();
        members.reserve(json_members.size());
        for (const auto &[cred, epoch]: json_members)
            members.try_emplace(credential_t::from_json(cred), json::value_to<uint64_t>(epoch));
        return {
            std::move(members),
            decltype(threshold)::from_json(j.at("threshold"))
        };
    }

    void pulsing_data_t::from_zpp(parallel_decoder &dec)
    {
        dec.add([&](const auto b) {
            zpp::deserialize(proposals, b);
        });
        dec.add([&](const auto b) {
            zpp::deserialize(drep_state, b);
        });
        dec.add([&](const auto b) {
            zpp::deserialize(committee_hot_keys, b);
        });
        dec.add([&](const auto b) {
            zpp::deserialize(drep_voting_power, b);
        });
        dec.add([&](const auto b) {
            zpp::deserialize(pool_voting_power, b);
        });
        dec.add([&](const auto b) {
            auto fields = std::tie(pool_default_votes, treasury);
            zpp::deserialize(fields, b);
        });
    }

    void pulsing_data_t::to_zpp(zpp_encoder &enc) const
    {
        enc.add([&](auto) {
            return zpp::serialize(proposals);
        });
        enc.add([&](auto) {
            return zpp::serialize(drep_state);
        });
        enc.add([&](auto) {
            return zpp::serialize(committee_hot_keys);
        });
        enc.add([&](auto) {
            return zpp::serialize(drep_voting_power);
        });
        enc.add([&](auto) {
            return zpp::serialize(pool_voting_power);
        });
        enc.add([&](auto) {
            return zpp::serialize(std::tie(pool_default_votes, treasury));
        });
    }

    void state::_decode_donations(cbor::zero2::value &v)
    {
        _donations = v.uint();
    }

    void state::_parse_protocol_params(protocol_params &params, cbor::zero2::value &v) const
    {
        _apply_shelley_params(params);
        _apply_alonzo_params(params);
        _apply_babbage_params(params);
        _apply_conway_params(params);
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
        params.protocol_ver = protocol_version::from_cbor(it.read());
        params.min_pool_cost = it.read().uint();
        params.lovelace_per_utxo_byte = it.read().uint();
        params.plutus_cost_models = decltype(params.plutus_cost_models)::from_cbor(it.read());
        params.ex_unit_prices = decltype(params.ex_unit_prices)::from_cbor(it.read());
        params.max_tx_ex_units = decltype(params.max_tx_ex_units)::from_cbor(it.read());
        params.max_block_ex_units = decltype(params.max_block_ex_units)::from_cbor(it.read());
        params.max_value_size = it.read().uint();
        params.max_collateral_pct = it.read().uint();
        params.max_collateral_inputs = it.read().uint();
        params.pool_voting_thresholds = decltype(params.pool_voting_thresholds)::from_cbor(it.read());
        params.drep_voting_thresholds = decltype(params.drep_voting_thresholds)::from_cbor(it.read());
        params.committee_min_size = it.read().uint();
        params.committee_max_term_length = it.read().uint();
        params.gov_action_lifetime = it.read().uint();
        params.gov_action_deposit = it.read().uint();
        params.drep_deposit = it.read().uint();
        params.drep_activity = it.read().uint();
        params.min_fee_ref_script_cost_per_byte = decltype(params.min_fee_ref_script_cost_per_byte)::from_cbor(it.read());
    }

    void state::_decode_protocol_state(cbor::zero2::value &v)
    {
        _certificate_tx.reset();
        _pending_drep_removals.clear();
        auto &it = v.array();
        _ppups.clear();
        _ppups_future.clear();

        // Conway replaces the two legacy protocol-update maps with proposals,
        // committee, and constitution state before the protocol parameters.
        it.skip(3);
        _parse_protocol_params(_params, it.read());
        _parse_protocol_params(_params_prev, it.read());
        _enact_state.params = _params;
        _enact_state.prev_params = _params_prev;

        auto &future_v = it.read();
        auto &future_it = future_v.array();
        if (const auto present = future_it.read().uint(); present == 1) {
            _parse_protocol_params(_ratify_state.new_state.params, future_it.read());
        } else if (present == 0) {
            _ratify_state.new_state.params = _params;
        } else {
            throw error{"unsupported future protocol-parameter discriminator: {}", present};
        }
    }

    void state::to_zpp(zpp_encoder &ser) const
    {
        if (!_pending_drep_removals.empty()) [[unlikely]]
            throw error("finish certificates before saving Conway state");
        ser.add([&](auto) {
            auto bytes = zpp::serialize(_enact_state);
            // 1 fixed PV11 pool VRF rules; 2 also fixes the DRep payout snapshot.
            bytes << zpp::serialize(uint8_t { 2 });
            return bytes;
        });
        ser.add([&](auto) {
            return zpp::serialize(_ratify_state);
        });
        _pulsing_data.to_zpp(ser);
        ser.add([&](auto) {
            return zpp::serialize(_committee_hot_keys);
        });
        ser.add([&](auto) {
            return zpp::serialize(_drep_state);
        });
        ser.add([&](auto) {
            return zpp::serialize(_num_dormant_epochs);
        });
        ser.add([&](auto) {
            return zpp::serialize(_proposals);
        });
        ser.add([&](auto) {
            return zpp::serialize(_donations);
        });
        ser.add([&](auto) {
            return zpp::serialize(_conway_start_epoch);
        });
        ser.add([&](auto) {
            return zpp::serialize(_ratify_ready);
        });
        babbage::state::to_zpp(ser);
    }

    void state::from_zpp(parallel_decoder &dec)
    {
        _certificate_tx.reset();
        _pending_drep_removals.clear();
        dec.add([&](const auto b) {
            const auto consumed = zpp::deserialize(_enact_state, b);
            const auto version = consumed < b.size() ? zpp::deserialize<uint8_t>(b.subbuf(consumed)) : 0;
            if (version > 2)
                throw error("unsupported Conway snapshot version");
            // Pre-PV11 states remain valid replay bases. Older PV11 states can
            // contain incorrect VRF counts or a DRep distribution missing payouts.
            if (version < 2 && _enact_state.params.protocol_ver.major >= 11)
                throw error("replay required: snapshot predates the PV11 VRF/DRep fixes");
        });
        dec.add([&](const auto b) {
            zpp::deserialize(_ratify_state, b);
        });
        _pulsing_data.from_zpp(dec);
        dec.add([&](const auto b) {
            zpp::deserialize(_committee_hot_keys, b);
        });
        dec.add([&](const auto b) {
            zpp::deserialize(_drep_state, b);
        });
        dec.add([&](const auto b) {
            zpp::deserialize(_num_dormant_epochs, b);
        });
        dec.add([&](const auto b) {
            zpp::deserialize(_proposals, b);
        });
        dec.add([&](const auto b) {
            zpp::deserialize(_donations, b);
        });
        dec.add([&](const auto b) {
            zpp::deserialize(_conway_start_epoch, b);
        });
        dec.add([&](const auto b) {
            zpp::deserialize(_ratify_ready, b);
        });
        babbage::state::from_zpp(dec);
        dec.on_done([this] {
            if (!_ratify_ready)
                return;
            // Older snapshots used incomplete parameter voting groups. Rebuild
            // only the pending ratification result, using the saved frozen inputs;
            // this does not replay blocks or recompute stake distributions.
            const auto affected = std::any_of(
                _pulsing_data.proposals.begin(), _pulsing_data.proposals.end(), [](const auto &entry) {
                    const auto *change = std::get_if<gov_action_t::parameter_change_t>(
                        &entry.second.proposal.action.val);
                    return change && (change->update.min_pool_cost.has_value()
                        || change->update.expansion_rate.has_value()
                        || change->update.treasury_growth_rate.has_value()
                        || change->update.pool_pledge_influence.has_value());
                });
            if (affected) {
                _ratify_state = {};
                _ratify_state.new_state = _enact_state;
                // EPOCH updates this field after copying the enacted state.
                _ratify_state.new_state.prev_params = _params_prev;
                _ratify_ready = false;
                _gov_finalize();
            }
        });
    }
}
