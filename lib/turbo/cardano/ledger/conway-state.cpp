/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/ledger/conway.hpp>
#include <turbo/cardano/ledger/conway/detail.hpp>
#include <turbo/cardano/ledger/updates.hpp>

namespace turbo::cardano::ledger::conway {
    vrf_state::vrf_state(babbage::vrf_state &&o): babbage::vrf_state { std::move(o) }
    {
        _max_epoch_slot = _cfg.shelley_epoch_length - _cfg.shelley_randomness_stabilization_window;
        logger::debug("conway::vrf_state created max_epoch_slot: {}", _max_epoch_slot);
    }

    state::state(): state { babbage::state { shelley::state { cardano::config::get(), scheduler::get() } } }
    {
        const protocol_version pv { 9, 0 };
        _params.protocol_ver = pv;
        _params_prev.protocol_ver = pv;
        _ratify_state.new_state.params.protocol_ver = pv;
        _num_dormant_epochs = 0;
    }

    state::state(babbage::state &&o):
        babbage::state { std::move(o) },
        _enact_state {
            committee_t { _cfg.conway_committee_members, _cfg.conway_committee_threshold },
            _cfg.conway_constitution
        }
    {
        _apply_conway_params(_params);
        _params_prev = _params;
        // The wrapper transitions from Babbage to Conway after the epoch boundary has
        // already been processed. Count that first Conway epoch as dormant here; no
        // Conway governance proposals can exist before the era starts.
        _num_dormant_epochs = 1;
        static const std::string task_name { "conway-update-utxos" };
        _sched.wait_all(task_name,
            [&](const auto &, const auto &submit_f) {
                for (size_t part_idx = 0; part_idx < txo_map::num_parts; ++part_idx) {
                    submit_f({1000, task_name, [this, part_idx] {
                        auto &utxo_part = _utxo.partition(part_idx);
                        for (auto &&[txo_id, txo_data]: utxo_part) {
                            if (const auto addr = txo_data.addr(); addr.has_pointer()) {
                                const auto ptr = addr.pointer();
                                if (ptr.slot > slot::from_epoch(_epoch, _cfg)
                                        || ptr.tx_idx >= std::numeric_limits<uint16_t>::max()
                                        || ptr.cert_idx >= std::numeric_limits<uint16_t>::max()) {
                                    txo_data.address_raw.resize(29);
                                    txo_data.address_raw << uint8_t { 0 } << uint8_t { 0 } << uint8_t { 0 };
                                }
                            }
                        }
                    }});
                }
            }
        );

        _enact_state.params = _params;
        _enact_state.prev_params = _params_prev;
        _enact_state.treasury = 0;
        _ratify_state.new_state = _enact_state;
        _gov_make_pulsing_snapshot();
    }

    void state::process_cert(const cert_t &cert, const cert_loc_t &loc)
    {
        _tick(loc.slot);
        std::visit([&](const auto &c) {
            process_cert(c, loc);
        }, cert.val);
    }

    bool state::has_drep(const credential_t &id) const
    {
        return _drep_state.contains(id);
    }

    void state::_tick(const uint64_t slot)
    {
        babbage::state::_tick(slot);
        if (!_pulsing_data.drep_state_updated && slot > _pulsing_snapshot_slot) {
            logger::debug("slot: {} creating drep pulser snapshots", cardano::slot { slot, _cfg });
            _pulsing_data.drep_state_updated = true;
        }
    }

    void state::_apply_conway_params(protocol_params &p) const
    {
        const auto &initial = _cfg.conway_protocol_params;
        p.plutus_cost_models.items.emplace(2, initial.plutus_cost_models.at(2));
        p.pool_voting_thresholds = initial.pool_voting_thresholds;
        p.drep_voting_thresholds = initial.drep_voting_thresholds;
        p.committee_min_size = initial.committee_min_size;
        p.committee_max_term_length = initial.committee_max_term_length;
        p.gov_action_lifetime = initial.gov_action_lifetime;
        p.gov_action_deposit = initial.gov_action_deposit;
        p.drep_deposit = initial.drep_deposit;
        p.drep_activity = initial.drep_activity;
        p.min_fee_ref_script_cost_per_byte = initial.min_fee_ref_script_cost_per_byte;
    }

    void state::_process_block_updates(block_update_list &&block_updates)
    {
        for (const auto &bu: block_updates)
            _donations += bu.donations;
        babbage::state::_process_block_updates(std::move(block_updates));
    }

    void state::_process_timed_update(tx_out_ref_list &collected_collateral, uint64_t &collateral_refund, timed_update_t &&upd)
    {
        std::visit([&](const auto &u) {
            using T = std::decay_t<decltype(u)>;
            if constexpr (std::is_same_v<T, reg_cert>
                || std::is_same_v<T, stake_reg_deleg_cert>
                || std::is_same_v<T, vote_reg_deleg_cert>
                || std::is_same_v<T, stake_vote_reg_deleg_cert>
                || std::is_same_v<T, reg_drep_cert>
                || std::is_same_v<T, vote_deleg_cert>
                || std::is_same_v<T, stake_vote_deleg_cert>
                || std::is_same_v<T, auth_committee_hot_cert>
                || std::is_same_v<T, resign_committee_cold_cert>
                || std::is_same_v<T, update_drep_cert>
                || std::is_same_v<T, unreg_cert>
                || std::is_same_v<T, unreg_drep_cert>) {
                process_cert(u, upd.loc);
            } else if constexpr (std::is_same_v<T, proposal_t>) {
                process_proposal(u, upd.loc);
            } else if constexpr (std::is_same_v<T, vote_info_t>) {
                process_vote(u, upd.loc);
            } else if constexpr (std::is_same_v<T, index::timed_update::conway_tx_prelude>) {
                _process_tx_prelude(u, upd.loc);
            } else if constexpr (std::is_same_v<T, index::timed_update::stake_withdraw>) {
                if (_params.protocol_ver.major < 11)
                    withdraw_reward(u.stake_id, u.amount);
            } else {
                babbage::state::_process_timed_update(collected_collateral, collateral_refund, std::move(upd));
            }
        }, upd.update);
    }

    void state::_process_tx_prelude(
        const index::timed_update::conway_tx_prelude &prelude,
        const cert_loc_t &loc)
    {
        if (_params.protocol_ver.major < 11)
            return;

        const auto current_epoch = slot { loc.slot, _cfg }.epoch();
        if (prelude.has_proposals && _num_dormant_epochs) {
            for (auto &[drep_id, info]: _drep_state) {
                static_cast<void>(drep_id);
                const auto actual_expiry = info.expire_epoch + _num_dormant_epochs;
                if (actual_expiry >= current_epoch)
                    info.expire_epoch = actual_expiry;
            }
            _num_dormant_epochs = 0;
        }
        for (const auto &drep_id: prelude.voting_dreps) {
            if (auto it = _drep_state.find(drep_id); it != _drep_state.end()) {
                it->second.expire_epoch = drep_info_t::compute_expire_epoch(
                    _params,
                    current_epoch,
                    _num_dormant_epochs);
            }
        }
        for (const auto &withdrawal: prelude.withdrawals)
            _withdraw_reward(withdrawal.reward_id, withdrawal.amount);
    }

    bool state::has_gov_action(const gov_action_id_t &gid) const
    {
        return _proposals.contains(gid);
    }

    const gov_action_state_t &state::gov_action(const gov_action_id_t &gid) const
    {
        return detail::map_nice_at(_proposals, gid);
    }

    const optional_committee_t &state::committee() const
    {
        return _enact_state.committee;
    }
}
