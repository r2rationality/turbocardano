/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Epoch.
// Included at namespace scope by lib/turbo/cardano/ledger/shelley.cpp.

    void state::start_epoch(std::optional<uint64_t> new_epoch)
    {
        logger::debug("state::start_epoch: prev_epoch: {} new_epoch: {}", _epoch, new_epoch);
        run_pulser_if_ready();
        if (!new_epoch) {
            // increment the epoch only if seen some data
            if (_end_offset)
                new_epoch = _epoch + 1;
            else
                new_epoch = 0;
        }
        if (*new_epoch < _epoch || *new_epoch > _epoch + 1) [[unlikely]]
            throw error(fmt::format("unexpected new epoch value: {} the current epoch: {}", *new_epoch, _epoch));;
        _epoch = *new_epoch;
        _epoch_slot = 0;
        _apply_future_shelley_delegs(cardano::slot::from_epoch(_epoch, _cfg));
        _reset_shelley_delegs_schedule();
        const auto prev_params = _apply_param_updates();
        if (_params.protocol_ver.major >= 2) {
            {
                const auto delta_ireserves = _transfer_instant_rewards(_instant_rewards_reserves);
                _reserves -= delta_ireserves;
                const auto delta_itreasury = _transfer_instant_rewards(_instant_rewards_treasury);
                _treasury -= delta_itreasury;
                logger::debug("delta_ireserves: {} delta_itreasury: {}", delta_ireserves, delta_itreasury);
            }
            _transfer_potential_rewards(prev_params);
            rotate_snapshots();
            _prep_op_stake_dist();
            _apply_future_pool_params();
            _nonmyopic = std::move(_nonmyopic_next);
            _nonmyopic_reward_pot = _reward_pot;
            _reserves -= _delta_reserves;
            _delta_reserves = 0;
            _treasury += _delta_treasury;
            _delta_treasury = 0;
            _reward_pot = 0;
            _rewards_ready = false;
            _blocks_past_voting_deadline = 0;
            _clean_old_epoch_data();
            _fees_utxo -= _delta_fees;
            _delta_fees = _fees_next_reward;
            _fees_next_reward = 0;
            _pulsing_snapshot_slot = cardano::slot::from_epoch(_epoch, _cfg) + _cfg.shelley_randomness_stabilization_window;
            const auto [refunds_user, refunds_treasury] = _retire_pools();
            logger::debug("epoch {} start: treasury: {} reserves: {} user refunds: {} treasury refunds: {}",
                _epoch, cardano::amount { _treasury }, cardano::amount { _reserves },
                cardano::amount { refunds_user }, cardano::amount { refunds_treasury });
        }
    }

    void state::_apply_future_pool_params()
    {
        for (auto &&[pool_id, params]: _future_pool_params) {
            auto &active = _active_pool_params.at(pool_id);
            if (_params.protocol_ver.major >= 11)
                _remove_pool_vrf_key_hash(active.params.vrf_vkey);
            active = std::move(params);
        }
        _future_pool_params.clear();
    }

    void state::_clean_old_epoch_data()
    {
        const auto potential_rewards_size = _potential_rewards.size();
        timer t { fmt::format("validator::state epoch: {} clean_old_epoch_data potential_rewards: {}", _epoch, potential_rewards_size), logger::level::trace };
        _blocks_before = std::move(_blocks_current);
        _blocks_current.clear();
        _reward_pulsing_snapshot.clear();
        if (potential_rewards_size) {
            const std::string task_group = fmt::format("ledger-state:clean-potential-rewards:epoch-{}", _epoch);
            _sched.wait_all(task_group,
                [&](const auto &, const auto &submit_f) {
                    for (size_t part_idx = 0; part_idx < _potential_rewards.num_parts; ++part_idx) {
                        submit_f({1000, task_group, [this, part_idx] {
                            _potential_rewards.partition(part_idx).clear();
                        }});
                    }
                });
        }
    }
