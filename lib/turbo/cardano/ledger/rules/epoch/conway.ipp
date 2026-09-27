/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Epoch.
// Included at namespace scope by lib/turbo/cardano/ledger/conway-epoch.cpp.

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

    void state::start_epoch(const std::optional<uint64_t> new_epoch)
    {
        finish_certificates();
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

        // EPOCH uses emptiness after removals, not the expiry of retained actions.
        if (_proposals.empty())
            ++_num_dormant_epochs;

        _gov_make_pulsing_snapshot(true);
        _ratify_ready = false;
    }
