/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Ratify.
// Included at namespace scope by lib/turbo/cardano/ledger/conway-epoch.cpp.

    drep_distr_t state::_compute_drep_voting_power() const
    {
        drep_distr_t power {};
        static const std::string task_id { "drep-voting-power" };
        mutex::unique_lock::mutex_type drep_mutex alignas(mutex::alignment) {};
        _sched.wait_all(task_id, [&](const auto &todo, const auto &submit_f) {
            for (size_t part_no = 0; part_no < _accounts.num_parts; ++part_no) {
                submit_f({ 1000, task_id, [&, part_no, todo] {
                    drep_distr_t part_stake {};
                    const auto &account_part = _accounts.partition(part_no);
                    for (const auto &[stake_id, info]: account_part) {
                        static_cast<void>(stake_id);
                        if (info.vote_deleg
                                && (!std::holds_alternative<credential_t>(
                                        info.vote_deleg->val)
                                    || _drep_state.contains(
                                        std::get<credential_t>(info.vote_deleg->val)))) {
                            // SNAP captured input Rewards before POOLREAP and governance payouts.
                            part_stake[*info.vote_deleg] += info.mark_stake;
                        }
                    }
                    mutex::scoped_lock lock { drep_mutex };
                    for (const auto &[drep, stake]: part_stake)
                        power[drep] += stake;
                }});
            }
        });
        for (const auto &[gid, action]: _proposals) {
            static_cast<void>(gid);
            const auto acc_it = _accounts.find(action.proposal.return_addr);
            if (acc_it != _accounts.end() && acc_it->second.vote_deleg) {
                const auto &drep = *acc_it->second.vote_deleg;
                if (!std::holds_alternative<credential_t>(drep.val)
                        || _drep_state.contains(std::get<credential_t>(drep.val))) {
                    power[drep] += action.proposal.deposit;
                }
            }
        }
        return power;
    }

    pool_stake_distribution state::_compute_pool_voting_power() const
    {
        pool_stake_distribution power {};
        for (const auto &[pool_id, stake]: _mark.pool_dist) {
            if (_mark.pool_params.contains(pool_id)
                    && _mark.delegated_pools.contains(pool_id)) {
                power.create(pool_id);
                power.add(pool_id, stake);
            }
        }
        for (const auto &[gid, action]: _proposals) {
            static_cast<void>(gid);
            const auto acc_it = _accounts.find(action.proposal.return_addr);
            if (acc_it != _accounts.end()
                    && acc_it->second.mark_deleg
                    && _mark.pool_params.contains(*acc_it->second.mark_deleg)) {
                power.create(*acc_it->second.mark_deleg);
                power.add(*acc_it->second.mark_deleg, action.proposal.deposit);
            }
        }
        return power;
    }

    void state::_snapshot_pool_default_votes()
    {
        auto &votes = _pulsing_data.pool_default_votes;
        votes.clear();
        votes.reserve(_mark.pool_params.size());
        for (const auto &[pool, info]: _mark.pool_params) {
            const auto acc = _accounts.find(info.params.reward_id);
            auto vote = default_vote_t::no;
            if (acc != _accounts.end() && acc->second.vote_deleg) {
                if (std::holds_alternative<drep_t::abstain_t>(acc->second.vote_deleg->val))
                    vote = default_vote_t::abstain;
                else if (std::holds_alternative<drep_t::no_confidence_t>(acc->second.vote_deleg->val))
                    vote = default_vote_t::no_confidence;
            }
            votes.emplace_hint(votes.end(), pool, vote);
        }
    }

    void state::rotate_snapshots()
    {
        babbage::state::rotate_snapshots();
        // Freeze defaults before POOLREAP, future pool parameters, and payouts.
        _snapshot_pool_default_votes();
    }

    void state::_gov_make_pulsing_snapshot(const bool pool_defaults_ready)
    {
        if (!pool_defaults_ready)
            _snapshot_pool_default_votes();
        _pulsing_data.treasury = _treasury;
        _pulsing_data.drep_state = _drep_state;
        _pulsing_data.committee_hot_keys = _committee_hot_keys;
        _pulsing_data.proposals.clear();
        _pulsing_data.proposals.reserve(_proposals.size());
        for (const auto &[gid, action]: _proposals)
            _pulsing_data.proposals.emplace_back(gid, action);
        std::sort(
            _pulsing_data.proposals.begin(),
            _pulsing_data.proposals.end(),
            [](const auto &left, const auto &right) {
                if (const auto cmp = left.second.proposal.action.priority()
                        - right.second.proposal.action.priority();
                        cmp != 0) {
                    return cmp < 0;
                }
                return left.second.loc < right.second.loc;
            });
        _pulsing_data.pool_voting_power = _compute_pool_voting_power();
        _pulsing_data.drep_voting_power = _compute_drep_voting_power();
    }

    void state::complete_pulsers()
    {
        finish_certificates();
        babbage::state::complete_pulsers();
        if (!_ratify_ready)
            _gov_finalize();
    }
