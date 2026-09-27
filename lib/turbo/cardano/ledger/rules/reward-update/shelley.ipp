/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.RewardUpdate.
// Included at namespace scope by lib/turbo/cardano/ledger/shelley.cpp.

    void state::run_pulser_if_ready()
    {
        if (_params.protocol_ver.major >= 2 && !_rewards_ready) {
            const auto slot = cardano::slot::from_epoch(_epoch, _cfg) + _epoch_slot;
            _ensure_reward_pulsing_snapshot(slot);
        }
        const auto run = _params.protocol_ver.major >= 2 && _epoch_slot >= _cfg.shelley_rewards_ready_slot && !_rewards_ready;
        if (run)
            _compute_rewards();
    }

    void state::_ensure_reward_pulsing_snapshot(const uint64_t slot)
    {
        if (!_params_prev.protocol_ver.forgo_reward_prefilter() && slot > _pulsing_snapshot_slot
                && _reward_pulsing_snapshot.empty() && !_accounts.empty()) {
            timer t { fmt::format("epoch: {} create a pulsing snapshot of reward accounts", _epoch), logger::level::debug };
            _reward_pulsing_snapshot.reserve(_accounts.size());
            for (const auto &[stake_id, acc]: _accounts) {
                if (acc.ptr)
                    _reward_pulsing_snapshot.emplace_back(stake_id, acc.reward);
            }
        }
    }

    void state::_transfer_potential_rewards(const cardano::protocol_params &params_prev)
    {
        const auto aggregated = params_prev.protocol_ver.aggregated_rewards();
        const auto forgo_prefilter = params_prev.protocol_ver.forgo_reward_prefilter();
        const bool force_active = !aggregated || forgo_prefilter;
        static constexpr uint64_t diagnostic_reward_threshold = 1'000'000'000'000;
        timer t { fmt::format("validator::state epoch: {} transfer_potential_rewards aggregated forgo_prefilter: {}", _epoch, forgo_prefilter), logger::level::debug };
        using pool_index_map = std::unordered_map<cardano::pool_hash, size_t>;
        const std::string task_group = fmt::format("ledger-state:transfer-rewards:epoch-{}", _epoch);
        const auto num_active_pools = _active_pool_dist.size();
        pool_index_map active_pool_indices {};
        std::vector<uint64_t> part_pool_updates {};
        active_pool_indices.reserve(num_active_pools);
        size_t active_pool_idx = 0;
        for (const auto &[pool_id, stake]: _active_pool_dist) {
            static_cast<void>(stake);
            active_pool_indices.try_emplace(pool_id, active_pool_idx++);
        }
        part_pool_updates.resize(_potential_rewards.num_parts * num_active_pools);
        const pool_index_map &pool_indices = active_pool_indices;
        std::array<uint64_t, partitioned_reward_update_dist::num_parts> treasury_updates {};
        // all rewards must be already created to ensure no allocation is necessary
        _sched.wait_all(task_group,
            [&](const auto &, const auto &submit_f) {
                for (size_t part_idx = 0; part_idx < _potential_rewards.num_parts; ++part_idx) {
                    submit_f({ 1000, task_group, [this, &pool_indices, &part_pool_updates, &treasury_updates,
                            num_active_pools, part_idx, aggregated, force_active] {
                        partitioned_reward_update_dist::partition_type reward_part {};
                        reward_part.swap(_potential_rewards.partition(part_idx));
                        // relies on reward_part, _accounts, and _pulsing_snapshot being ordered containers!
                        auto acc_it = _accounts.partition(part_idx).begin();
                        const auto acc_end = _accounts.partition(part_idx).end();
                        const auto pool_update_offset = part_idx * num_active_pools;
                        uint64_t part_treasury_update = 0;
                        for (const auto &[stake_id, reward_list]: reward_part) {
                            uint64_t total_reward = 0;
                            for (const auto &ri: reward_list)
                                total_reward += ri.amount;
                            const bool eligible = force_active || _reward_pulsing_snapshot.contains(stake_id);
                            if (eligible) {
                                while (acc_it != acc_end && acc_it->first < stake_id)
                                    ++acc_it;
                                if (total_reward >= diagnostic_reward_threshold) {
                                    logger::debug("epoch: {} potential_rewards stake_id: {} total: {} account_known: {} registered: {} prev_reward: {} delegated: {} items: {}",
                                        _epoch, stake_id, cardano::amount { total_reward },
                                        acc_it != acc_end && acc_it->first == stake_id,
                                        acc_it != acc_end && acc_it->first == stake_id && acc_it->second.ptr.has_value(),
                                        cardano::amount { acc_it != acc_end && acc_it->first == stake_id ? acc_it->second.reward : 0 },
                                        acc_it != acc_end && acc_it->first == stake_id && acc_it->second.deleg.has_value(),
                                        reward_list.size());
                                }
                                const bool to_reward_account = acc_it != acc_end && acc_it->first == stake_id && acc_it->second.ptr;
                                uint64_t account_reward = 0;
                                for (auto &&ri: reward_list) {
                                    if (ri.amount) {
                                        if (total_reward >= diagnostic_reward_threshold || ri.amount >= diagnostic_reward_threshold) {
                                            logger::debug("epoch: {} potential_reward_transfer stake_id: {} type: {} pool_id: {} amount: {} destination: {} delegated_pool_id: {}",
                                                _epoch, stake_id, ri.type, ri.pool_id, cardano::amount { ri.amount },
                                                to_reward_account ? "reward-account" : "treasury", ri.delegated_pool_id);
                                        }
                                        if (to_reward_account)
                                            account_reward += ri.amount;
                                        else
                                            part_treasury_update += ri.amount;
                                        if (!aggregated)
                                            break;
                                    }
                                }
                                if (account_reward) {
                                    acc_it->second.reward += account_reward;
                                    // Rewards become part of the stake snapshot at the epoch boundary,
                                    // so they must follow the account's delegation at that boundary. The
                                    // delegation captured while calculating the reward can be stale after
                                    // a late-epoch redelegation.
                                    if (acc_it->second.deleg
                                            && _active_pool_params.contains(*acc_it->second.deleg)) {
                                        const auto pool_it = pool_indices.find(*acc_it->second.deleg);
                                        if (pool_it == pool_indices.end()) [[unlikely]] {
                                            throw error(fmt::format(
                                                "active reward delegation pool {} is missing from the stake distribution",
                                                *acc_it->second.deleg));
                                        }
                                        part_pool_updates[pool_update_offset + pool_it->second] += account_reward;
                                    }
                                }
                            } else if (total_reward >= diagnostic_reward_threshold) {
                                logger::debug("epoch: {} potential_rewards stake_id: {} total: {} skipped inactive account",
                                    _epoch, stake_id, cardano::amount { total_reward });
                            }
                        }
                        treasury_updates[part_idx] = part_treasury_update;
                    }});
                }
            });
        uint64_t treasury_update = 0;
        for (const auto amount: treasury_updates)
            treasury_update += amount;
        logger::debug("epoch {} transfer_potential_rewards treasury_update: {}", _epoch, treasury_update);
        _treasury += treasury_update;
        {
            std::vector<uint64_t> pool_updates(num_active_pools, 0);
            for (size_t part_idx = 0; part_idx < _potential_rewards.num_parts; ++part_idx) {
                const auto pool_update_offset = part_idx * num_active_pools;
                for (size_t pool_idx = 0; pool_idx < num_active_pools; ++pool_idx)
                    pool_updates[pool_idx] += part_pool_updates[pool_update_offset + pool_idx];
            }
            size_t pool_idx = 0;
            for (auto pool_it = _active_pool_dist.begin(); pool_it != _active_pool_dist.end(); ++pool_it)
                _active_pool_dist.add(pool_it, pool_updates[pool_idx++]);
        }
    }
