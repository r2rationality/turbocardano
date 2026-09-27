/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Rewards.
// Included at namespace scope by lib/turbo/cardano/ledger/shelley.cpp.

    uint64_t state::_total_stake(uint64_t reserves) const
    {
        return _cfg.shelley_max_lovelace_supply - reserves;
    }

    void state::_compute_rewards()
    {
        timer t { fmt::format("compute rewards for epoch {}", _epoch), logger::level::debug };
        _rewards_ready = true;
        uint64_t expansion = 0;
        if (rational_from_r64(_params_prev.decentralization) < rational_from_r64(_params_prev.decentralizationThreshold) && _epoch > 0) {
            cpp_rational perf = std::min(cpp_rational { 1 }, cpp_rational { _blocks_before.total_stake() } / ((1 - rational_from_r64(_params_prev.decentralization)) * _cfg.shelley_epoch_blocks));
            expansion = static_cast<uint64_t>(rational_from_r64(_params_prev.expansion_rate) * _reserves * perf);
            logger::trace("epoch: {} performance-adjusted expansion: {} perf: {} d: {} blocks: {}",
                _epoch, expansion, perf, _params_prev.decentralization, _blocks_before.total_stake());
        } else {
            expansion = static_cast<uint64_t>(rational_from_r64(_params_prev.expansion_rate) * _reserves);
            logger::trace("epoch: {} simple expansion: {}", _epoch, expansion);
        }
        const uint64_t total_reward_pool = expansion + _delta_fees;
        const uint64_t treasury_rewards = static_cast<uint64_t>(rational_from_r64(_params_prev.treasury_growth_rate) * total_reward_pool);
        _reward_pot = total_reward_pool - treasury_rewards;
        uint64_t pool_rewards_filtered = 0;
        const uint64_t total_stake = _total_stake(_reserves);
        if (!_blocks_before.empty()) {
            const auto &pools_active = _blocks_before;
            {
                timer t2 { fmt::format("compute per-pool rewards for epoch {}", _epoch), logger::level::trace };
                pool_rewards_filtered = _compute_pool_rewards_parallel(pools_active, _reward_pot, total_stake);
            }
            logger::trace("epoch {} total stake {} treasury: {} reserves: {} rewards pot: {} block-producing pools: {} reward pools: {} rewards attributed: {}",
                _epoch, total_stake, _treasury, _reserves, _reward_pot, pools_active.size(), _go.pool_params.size(), pool_rewards_filtered);
        }
        _delta_treasury = treasury_rewards;
        _delta_reserves = treasury_rewards + pool_rewards_filtered - _delta_fees;
        logger::debug("epoch {} deltaR ({}) = deltaT ({}) + poolRewards ({}) - deltaF {}",
            _epoch, cardano::amount { _delta_reserves }, cardano::amount { _delta_treasury },
            cardano::amount { pool_rewards_filtered }, cardano::amount { _delta_fees });
    }

    void state::_rewards_prepare_pool_params(uint64_t &total, uint64_t &filtered, const rational_u64 &z0,
        const uint64_t staking_reward_pot, const uint64_t total_stake, const pool_hash &pool_id,
        pool_info &info, const uint64_t pool_blocks)
    {
        const uint64_t pool_stake = _go.pool_dist.get(pool_id);
        uint64_t pool_reward_pot = 0;
        if (pool_stake > 0) {
            uint64_t leader_reward = 0;
            uint64_t owner_stake = 0;
            for (const auto &stake_id: info.params.owners) {
                if (const auto acc_it = _accounts.find(stake_id); acc_it != _accounts.end() && acc_it->second.go_deleg == pool_id)
                    owner_stake += acc_it->second.go_stake;
            }
            if (owner_stake >= info.params.pledge) {
                const cpp_rational z0_r = rational_from_r64(z0);
                const cpp_rational pool_rel_total_stake { pool_stake, std::max<uint64_t>(1, total_stake) };
                const cpp_rational sigma_mark = std::min(pool_rel_total_stake, z0_r);
                const cpp_rational pool_rel_active_stake { pool_stake, std::max<uint64_t>(1, _go.pool_dist.total_stake()) };
                const cpp_rational pledge_rel_total_stake { info.params.pledge, std::max<uint64_t>(1, total_stake) };
                if (pool_rel_total_stake < pledge_rel_total_stake) [[unlikely]]
                    throw error(fmt::format("internal error: pledged stake: {} of pool {} is larger than the pool's total stake: {}", info.params.pledge, pool_id, pool_stake));
                const cpp_rational s_mark = std::min(pledge_rel_total_stake, z0_r);
                const cpp_rational pool_pledge_influence = rational_from_r64(_params_prev.pool_pledge_influence);
                const cpp_rational factor1 = cpp_rational { staking_reward_pot } / (cpp_rational { 1 } + pool_pledge_influence);
                const cpp_rational factor4 = (z0_r - sigma_mark) / z0_r;
                const cpp_rational factor3 = (sigma_mark - s_mark * factor4) / z0_r;
                const cpp_rational factor2 = sigma_mark + s_mark * pool_pledge_influence * factor3;
                const uint64_t optimal_reward = static_cast<uint64_t>(factor1 * factor2);
                pool_reward_pot = optimal_reward;
                if (rational_from_r64(_params_prev.decentralization) < rational_from_r64(_params_prev.decentralizationThreshold)) {
                    const cpp_rational beta { pool_blocks, std::max<uint64_t>(1, _blocks_before.total_stake()) };
                    const cpp_rational pool_performance = pool_rel_active_stake != cpp_rational {} ? beta / pool_rel_active_stake : cpp_rational {};
                    pool_reward_pot = static_cast<uint64_t>(pool_performance * cpp_rational { optimal_reward });
                }
                if (pool_reward_pot > info.params.cost && owner_stake < pool_stake) {
                    cpp_rational &base = rational_from_storage(info.reward_base);
                    base = pool_reward_pot - info.params.cost;
                    base /= pool_stake;
                    base *= info.params.margin.denominator - info.params.margin.numerator;
                    base /= info.params.margin.denominator;
                    const cpp_rational pool_margin = rational_from_r64(info.params.margin);
                    const cpp_rational leader_reward_ratio = pool_margin
                        + (cpp_rational { 1 } - pool_margin) * cpp_rational { owner_stake, pool_stake };
                    leader_reward = info.params.cost + static_cast<uint64_t>(cpp_rational { pool_reward_pot - info.params.cost } * leader_reward_ratio);
                } else {
                    leader_reward = pool_reward_pot;
                }
            }
            const stake_ident reward_stake_id = info.params.reward_id;
            const bool leader_active = _params_prev.protocol_ver.forgo_reward_prefilter() || _reward_pulsing_snapshot.contains(reward_stake_id);
            if (leader_active) {
                auto &reward_list = _potential_rewards[reward_stake_id];
                total += leader_reward;
                if (!reward_list.empty())
                    filtered -= reward_list.begin()->amount;
                if (const auto acc_it = _accounts.find(reward_stake_id); acc_it != _accounts.end() && acc_it->second.deleg)
                    reward_list.emplace(reward_type::leader, pool_id, leader_reward, *acc_it->second.deleg);
                else
                    reward_list.emplace(reward_type::leader, pool_id, leader_reward);
                filtered += reward_list.begin()->amount;
            }
        }
    }

    std::pair<uint64_t, uint64_t> state::_rewards_prepare_pools(const pool_block_dist &pools_active, const uint64_t staking_reward_pot, const uint64_t total_stake)
    {
        uint64_t total = 0;
        uint64_t filtered = 0;
        const rational_u64 z0 { 1, _params_prev.n_opt };
        const cpp_rational z0_r = rational_from_r64(z0);
        _nonmyopic_next.clear();
        for (auto &[pool_id, pool_info]: _go.pool_params) {
            if (!_pbft_pools.contains(pool_id)) {
                const uint64_t pool_blocks = pools_active.get(pool_id);
                rational_from_storage(pool_info.reward_base) = cpp_rational {};
                if (pool_blocks > 0)
                    _rewards_prepare_pool_params(total, filtered, z0, staking_reward_pot, total_stake, pool_id, pool_info, pool_blocks);
                const cpp_rational rel_stake { _go.pool_dist.get(pool_id), total_stake };
                const auto rel_stake_bounded = std::min(z0_r, rel_stake);
                pool_rank::likelihood_prior prior {};
                if (const auto prior_it = _nonmyopic.find(pool_id); prior_it != _nonmyopic.end())
                    prior.emplace(prior_it->second);
                _nonmyopic_next.try_emplace(pool_id, pool_rank::likelihoods(pool_blocks, _cfg.shelley_epoch_length,
                    static_cast<double>(rel_stake), _cfg.shelley_active_slots, _params_prev.decentralization, prior));
            }
        }
        return std::make_pair(total, filtered);
    }

    std::pair<uint64_t, uint64_t> state::_rewards_compute_part(const size_t part_idx)
    {
        uint64_t total = 0;
        uint64_t filtered = 0;
        auto &part = _potential_rewards.partition(part_idx);
        const auto &acc_part = _accounts.partition(part_idx);
        for (const auto &[stake_id, acc]: acc_part) {
            if (acc.go_deleg) {
                const auto &pool_info = _go.pool_params.at(*acc.go_deleg);
                if (std::find(pool_info.params.owners.begin(), pool_info.params.owners.end(), stake_id) == pool_info.params.owners.end()) {
                    const uint64_t deleg_stake = acc.go_stake;
                    const auto &reward_base = rational_from_storage(pool_info.reward_base);
                    const uint64_t member_reward = static_cast<uint64_t>(reward_base * deleg_stake);
                    if (member_reward > 0) {
                        const bool active = _params_prev.protocol_ver.forgo_reward_prefilter() || _reward_pulsing_snapshot.contains(stake_id);
                        if (active) {
                            auto &reward_list = part[stake_id];
                            total += member_reward;
                            if (!reward_list.empty())
                                filtered -= reward_list.begin()->amount;
                            if (acc.deleg)
                                reward_list.emplace(reward_type::member, *acc.go_deleg, member_reward, *acc.deleg);
                            else
                                reward_list.emplace(reward_type::member, *acc.go_deleg, member_reward);
                            filtered += reward_list.begin()->amount;
                        }
                    }
                }
            }
        }
        return std::make_pair(total, filtered);
    }

    uint64_t state::_compute_pool_rewards_parallel(const pool_block_dist &pools_active, const uint64_t staking_reward_pot, const uint64_t total_stake)
    {
        const std::string task_group = fmt::format("ledger-state:compute-rewards:epoch-{}", _epoch);
        const auto [init_total, init_filtered] = _rewards_prepare_pools(pools_active, staking_reward_pot, total_stake);
        std::atomic_uint64_t total = init_total;
        std::atomic_uint64_t filtered = init_filtered;
        _sched.wait_all(task_group,
            [&](const auto &, const auto &submit_f) {
                for (size_t part_idx = 0; part_idx < _potential_rewards.num_parts; ++part_idx) {
                    submit_f({1000, task_group, [&total, &filtered, this, part_idx] {
                        const auto [part_total, part_filtered] = _rewards_compute_part(part_idx);
                        total.fetch_add(part_total, std::memory_order_relaxed);
                        filtered.fetch_add(part_filtered, std::memory_order_relaxed);
                    }});
                }
            });
        logger::trace("epoch: {} staking_rewards total: {} filtered: {} diff: {}",
            _epoch, cardano::amount { total.load() }, cardano::amount { filtered.load() },
            cardano::balance_change { static_cast<int64_t>(filtered) - static_cast<int64_t>(total) });
        if (_params_prev.protocol_ver.aggregated_rewards())
            return total;
        return filtered;
    }
