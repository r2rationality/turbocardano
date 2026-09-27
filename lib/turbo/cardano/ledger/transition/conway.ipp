/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Included at namespace scope by lib/turbo/cardano/ledger/conway-state.cpp.

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
