/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <algorithm>
#include <turbo/cardano/ledger/conway.hpp>
#include <turbo/cardano/ledger/conway/detail.hpp>
#include <turbo/cardano/ledger/conway/gov.hpp>

namespace turbo::cardano::ledger::conway {
    void state::_transfer_treasury_withdrawals(const stake_distribution &rewards)
    {
        for (const auto &[stake_id, reward]: rewards) {
            if (auto acc_it = _accounts.find(stake_id);
                    acc_it != _accounts.end() && acc_it->second.ptr) {
                auto &acc = acc_it->second;
                _treasury -= reward;
                acc.reward += reward;
                if (acc.deleg)
                    _active_pool_dist.add(*acc.deleg, reward);
            }
        }
    }

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
                            part_stake[*info.vote_deleg] += info.stake + info.reward;
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
                    && acc_it->second.deleg
                    && _mark.pool_params.contains(*acc_it->second.deleg)) {
                power.create(*acc_it->second.deleg);
                power.add(*acc_it->second.deleg, action.proposal.deposit);
            }
        }
        return power;
    }

    const pulsing_data_t &state::pulser_data() const
    {
        return _pulsing_data;
    }

    void state::_gov_remove_proposal(const gov_action_id_t &gid)
    {
        const auto &action = detail::map_nice_at(_proposals, gid);
        if (auto acc_it = _accounts.find(action.proposal.return_addr);
                acc_it != _accounts.end() && acc_it->second.ptr) {
            acc_it->second.reward += action.proposal.deposit;
            if (acc_it->second.deleg)
                _active_pool_dist.add(*acc_it->second.deleg, action.proposal.deposit);
        } else {
            _treasury += action.proposal.deposit;
        }
        _deposited -= action.proposal.deposit;
        _proposals.erase(gid);
    }

    void state::_gov_remove_with_descendants(const gov_action_id_t &gid)
    {
        if (!_proposals.contains(gid))
            return;
        set_t<gov_action_id_t> to_remove { gid };
        bool changed = true;
        while (changed) {
            changed = false;
            for (const auto &[child_gid, child_action]: _proposals) {
                if (to_remove.contains(child_gid))
                    continue;
                const auto parent = rules::gov::action_parent(child_action.proposal.action);
                const auto parent_it = parent ? _proposals.find(*parent) : _proposals.end();
                if (parent_it != _proposals.end()
                        && to_remove.contains(*parent)
                        && rules::gov::same_parent_group(
                            child_action.proposal.action,
                            parent_it->second.proposal.action)) {
                    to_remove.emplace(child_gid);
                    changed = true;
                }
            }
        }
        for (const auto &remove_gid: to_remove)
            _gov_remove_proposal(remove_gid);
    }

    // Applies the effects produced by RATIFY inside the Agda EPOCH transition.
    void state::_gov_enact()
    {
        for (const auto &gid: _ratify_state.expired)
            _gov_remove_with_descendants(gid);
        for (const auto &[gid, action]: _ratify_state.enacted) {
            if (!_proposals.contains(gid))
                continue;
            if (rules::gov::action_has_parent(action.proposal.action)) {
                const auto parent = rules::gov::action_parent(action.proposal.action);
                set_t<gov_action_id_t> siblings {};
                for (const auto &[other_gid, other_action]: _proposals) {
                    if (other_gid == gid)
                        continue;
                    if (!rules::gov::same_parent_group(
                            action.proposal.action,
                            other_action.proposal.action)) {
                        continue;
                    }
                    if (rules::gov::action_parent(other_action.proposal.action) == parent)
                        siblings.emplace(other_gid);
                }
                for (const auto &sibling: siblings)
                    _gov_remove_with_descendants(sibling);
                _gov_remove_proposal(gid);
            } else {
                _gov_remove_proposal(gid);
            }
        }

        _params_prev = _params;
        _enact_state = _ratify_state.new_state;
        _params = _enact_state.params;
        _transfer_treasury_withdrawals(_enact_state.withdrawals);
        _enact_state.withdrawals.clear();
        _ratify_state.enacted.clear();
        _ratify_state.expired.clear();
        _ratify_state.delayed = false;
        _ratify_state.new_state.prev_params = _params_prev;
        _ratify_state.new_state.withdrawals.clear();
        _ratify_state.new_state.treasury = 0;

        _treasury += _donations;
        _donations = 0;
    }

    void state::_gov_make_pulsing_snapshot()
    {
        _pulsing_data.drep_state_updated = false;
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

    void state::_prune_committee_hot_keys()
    {
        if (!_enact_state.committee) {
            _committee_hot_keys.clear();
            return;
        }
        for (auto it = _committee_hot_keys.begin(); it != _committee_hot_keys.end();) {
            if (!_enact_state.committee->members.contains(it->first))
                it = _committee_hot_keys.erase(it);
            else
                ++it;
        }
    }

    // EPOCH. This method is called for every Conway epoch except the first.
    void state::start_epoch(const std::optional<uint64_t> new_epoch)
    {
        babbage::state::start_epoch(new_epoch);
        _gov_enact();
        _prune_committee_hot_keys();

        if (_params.protocol_ver.major == 11 && _params_prev.protocol_ver.major < 11)
            _populate_pool_vrf_key_hashes();

        if (!_conway_start_epoch)
            _conway_start_epoch.emplace(_epoch);
        if (!_stake_pointers.empty() && _epoch > *_conway_start_epoch)
            _stake_pointers.clear();

        if (_params.protocol_ver.major >= 10 && _params_prev.protocol_ver.major < 10) {
            // Recreate delegation state after the protocol-version 9 ledger bug.
            for (auto &[drep, drep_state]: _drep_state) {
                static_cast<void>(drep);
                drep_state.delegs.clear();
            }
            for (auto &[stake_id, info]: _accounts) {
                if (info.vote_deleg
                        && std::holds_alternative<credential_t>(info.vote_deleg->val)) {
                    const auto &drep_id = std::get<credential_t>(info.vote_deleg->val);
                    if (const auto it = _drep_state.find(drep_id); it != _drep_state.end())
                        it->second.delegs.emplace(stake_id);
                    else
                        info.vote_deleg.reset();
                }
            }
        }

        const auto has_active_proposals = std::any_of(
            _proposals.begin(),
            _proposals.end(),
            [this](const auto &item) {
                return _epoch <= item.second.expires_after;
            });
        if (!has_active_proposals)
            ++_num_dormant_epochs;

        _gov_make_pulsing_snapshot();
        _ratify_ready = false;
    }

    void state::run_pulser_if_ready()
    {
        babbage::state::run_pulser_if_ready();
        if (_epoch_slot >= _cfg.shelley_rewards_ready_slot && !_ratify_ready)
            _gov_finalize();
    }
}
