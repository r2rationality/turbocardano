/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <algorithm>
#include <turbo/cardano/ledger/conway/gov.hpp>

namespace turbo::cardano::ledger::conway::rules::gov {
    namespace {
        std::string action_label_impl(const gov_action_t &ga)
        {
            return std::visit<std::string>([](const auto &v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, gov_action_t::parameter_change_t>)
                    return "parameter_change";
                if constexpr (std::is_same_v<T, gov_action_t::hard_fork_init_t>)
                    return "hard_fork";
                if constexpr (std::is_same_v<T, gov_action_t::treasury_withdrawals_t>)
                    return "treasury_withdrawals";
                if constexpr (std::is_same_v<T, gov_action_t::no_confidence_t>)
                    return "no_confidence";
                if constexpr (std::is_same_v<T, gov_action_t::update_committee_t>)
                    return "update_committee";
                if constexpr (std::is_same_v<T, gov_action_t::new_constitution_t>)
                    return "new_constitution";
                if constexpr (std::is_same_v<T, gov_action_t::info_action_t>)
                    return "info";
                throw error(fmt::format("unsupported gov action type: {}", typeid(T).name()));
                return "unsupported";
            }, ga.val);
        }

        bool protocol_version_can_follow(const protocol_version &prev, const protocol_version &next)
        {
            return (next.major == prev.major + 1 && next.minor == 0)
                || (next.major == prev.major && next.minor == prev.minor + 1);
        }

        bool param_update_well_formed(const param_update_t &upd, const protocol_version &pv)
        {
            bool has_update = false;
            const auto has = [&](const auto &v) {
                has_update |= v.has_value();
            };
            const auto non_zero = [&](const auto &v) {
                has(v);
                return !v || *v != 0;
            };

            if (!non_zero(upd.max_block_body_size)
                    || !non_zero(upd.max_transaction_size)
                    || !non_zero(upd.max_block_header_size)
                    || !non_zero(upd.max_value_size)
                    || !non_zero(upd.max_collateral_pct)
                    || !non_zero(upd.committee_max_term_length)
                    || !non_zero(upd.gov_action_lifetime)
                    || !non_zero(upd.pool_deposit)
                    || !non_zero(upd.gov_action_deposit)
                    || !non_zero(upd.drep_deposit))
                return false;
            if (!pv.bootstrap_phase() && upd.lovelace_per_utxo_byte && *upd.lovelace_per_utxo_byte == 0)
                return false;
            if (pv.major >= 11 && upd.n_opt && *upd.n_opt == 0)
                return false;

            has(upd.min_fee_a);
            has(upd.min_fee_b);
            has(upd.key_deposit);
            has(upd.e_max);
            has(upd.n_opt);
            has(upd.pool_pledge_influence);
            has(upd.expansion_rate);
            has(upd.treasury_growth_rate);
            has(upd.min_pool_cost);
            has(upd.lovelace_per_utxo_byte);
            has(upd.plutus_cost_models);
            has(upd.ex_unit_prices);
            has(upd.max_tx_ex_units);
            has(upd.max_block_ex_units);
            has(upd.max_collateral_inputs);
            has(upd.pool_voting_thresholds);
            has(upd.drep_voting_thresholds);
            has(upd.committee_min_size);
            has(upd.drep_activity);
            has(upd.min_fee_ref_script_cost_per_byte);
            has(upd.max_ref_script_size_per_block);
            has(upd.max_ref_script_size_per_tx);
            has(upd.ref_script_cost_stride);
            has(upd.ref_script_cost_multiplier);
            has(upd.max_pledge_leverage);
            has(upd.min_pool_margin);
            return has_update;
        }

        bool action_well_formed_impl(
            const gov_action_t &ga,
            const protocol_version &protocol_ver,
            const uint8_t network_id)
        {
            return std::visit<bool>([&](const auto &a) {
                using T = std::decay_t<decltype(a)>;
                if constexpr (std::is_same_v<T, gov_action_t::parameter_change_t>) {
                    return param_update_well_formed(a.update, protocol_ver);
                } else if constexpr (std::is_same_v<T, gov_action_t::treasury_withdrawals_t>) {
                    bool has_non_zero = false;
                    for (const auto &[reward_id, coin]: a.withdrawals) {
                        if (reward_id.network_id() != network_id)
                            return false;
                        has_non_zero |= coin != 0;
                    }
                    return has_non_zero;
                } else {
                    return true;
                }
            }, ga.val);
        }

        bool action_valid_impl(
            const gov_action_t &ga,
            const enact_state_t &enact_state,
            const uint64_t current_epoch)
        {
            return std::visit<bool>([&](const auto &a) {
                using T = std::decay_t<decltype(a)>;
                if constexpr (std::is_same_v<T, gov_action_t::parameter_change_t>) {
                    return a.policy_id == enact_state.constitution.policy_id;
                } else if constexpr (std::is_same_v<T, gov_action_t::treasury_withdrawals_t>) {
                    return a.policy_id == enact_state.constitution.policy_id;
                } else if constexpr (std::is_same_v<T, gov_action_t::update_committee_t>) {
                    for (const auto &[cred, expire_epoch]: a.members_to_add) {
                        if (expire_epoch <= current_epoch || a.members_to_remove.contains(cred))
                            return false;
                    }
                    return true;
                } else {
                    return true;
                }
            }, ga.val);
        }

        bool has_valid_parent_impl(
            const gov_action_t &ga,
            const enact_state_t &enact_state,
            const proposal_map &proposals)
        {
            const auto has_pending_parent = [&](const optional_gov_action_id_t &prev_action_id) {
                if (!prev_action_id)
                    return false;
                const auto it = proposals.find(*prev_action_id);
                return it != proposals.end() && same_parent_group(ga, it->second.proposal.action);
            };
            return std::visit<bool>([&](const auto &a) {
                using T = std::decay_t<decltype(a)>;
                if constexpr (std::is_same_v<T, gov_action_t::parameter_change_t>
                        || std::is_same_v<T, gov_action_t::hard_fork_init_t>
                        || std::is_same_v<T, gov_action_t::no_confidence_t>
                        || std::is_same_v<T, gov_action_t::update_committee_t>
                        || std::is_same_v<T, gov_action_t::new_constitution_t>) {
                    return prev_action_as_expected(ga, enact_state)
                        || has_pending_parent(a.prev_action_id);
                } else {
                    return true;
                }
            }, ga.val);
        }

        bool valid_hard_fork_impl(
            const gov_action_t &ga,
            const enact_state_t &enact_state,
            const proposal_map &proposals)
        {
            if (!std::holds_alternative<gov_action_t::hard_fork_init_t>(ga.val))
                return true;
            const auto &a = std::get<gov_action_t::hard_fork_init_t>(ga.val);
            protocol_version base_ver = enact_state.params.protocol_ver;
            if (a.prev_action_id
                    && a.prev_action_id != enact_state.prev_actions.hard_forks
                    && a.protocol_ver.major <= enact_state.params.protocol_ver.major + 1) {
                if (const auto prop_it = proposals.find(*a.prev_action_id);
                        prop_it != proposals.end()
                        && std::holds_alternative<gov_action_t::hard_fork_init_t>(
                            prop_it->second.proposal.action.val)) {
                    base_ver = std::get<gov_action_t::hard_fork_init_t>(
                        prop_it->second.proposal.action.val).protocol_ver;
                }
            }
            return protocol_version_can_follow(base_ver, a.protocol_ver);
        }

        bool can_vote_impl(
            const gov_action_t &ga,
            const voter_t::type_t voter_type,
            const protocol_version &protocol_ver)
        {
            const auto is_cc = voter_type == voter_t::type_t::const_comm_key
                || voter_type == voter_t::type_t::const_comm_script;
            const auto is_drep = voter_type == voter_t::type_t::drep_key
                || voter_type == voter_t::type_t::drep_script;
            const auto is_pool = voter_type == voter_t::type_t::pool_key;
            const auto allowed = std::visit<bool>([&](const auto &a) {
                using T = std::decay_t<decltype(a)>;
                if constexpr (std::is_same_v<T, gov_action_t::no_confidence_t>)
                    return is_drep || is_pool;
                if constexpr (std::is_same_v<T, gov_action_t::update_committee_t>)
                    return is_drep || is_pool;
                if constexpr (std::is_same_v<T, gov_action_t::new_constitution_t>)
                    return is_cc || is_drep;
                if constexpr (std::is_same_v<T, gov_action_t::hard_fork_init_t>)
                    return is_cc || is_drep || is_pool;
                if constexpr (std::is_same_v<T, gov_action_t::parameter_change_t>)
                    return is_cc || is_drep || (is_pool && a.update.security_group());
                if constexpr (std::is_same_v<T, gov_action_t::treasury_withdrawals_t>)
                    return is_cc || is_drep;
                if constexpr (std::is_same_v<T, gov_action_t::info_action_t>)
                    return is_cc || is_drep || is_pool;
                throw error(fmt::format("unsupported governance action type: {}", typeid(T).name()));
                return false;
            }, ga.val);
            if (!allowed)
                return false;
            if (protocol_ver.bootstrap_phase()) {
                if (is_drep)
                    return std::holds_alternative<gov_action_t::info_action_t>(ga.val);
                return is_bootstrap_action(ga);
            }
            return true;
        }
    }

    std::string action_label(const gov_action_t &ga)
    {
        return action_label_impl(ga);
    }

    bool action_well_formed(
        const gov_action_t &ga,
        const protocol_version &protocol_ver,
        const uint8_t network_id)
    {
        return action_well_formed_impl(ga, protocol_ver, network_id);
    }

    bool action_valid(
        const gov_action_t &ga,
        const enact_state_t &enact_state,
        const uint64_t current_epoch)
    {
        return action_valid_impl(ga, enact_state, current_epoch);
    }

    bool has_valid_parent(
        const gov_action_t &ga,
        const enact_state_t &enact_state,
        const proposal_map &proposals)
    {
        return has_valid_parent_impl(ga, enact_state, proposals);
    }

    bool valid_hard_fork(
        const gov_action_t &ga,
        const enact_state_t &enact_state,
        const proposal_map &proposals)
    {
        return valid_hard_fork_impl(ga, enact_state, proposals);
    }

    bool can_vote(
        const gov_action_t &ga,
        const voter_t::type_t voter_type,
        const protocol_version &protocol_ver)
    {
        return can_vote_impl(ga, voter_type, protocol_ver);
    }

    bool same_parent_group(const gov_action_t &l, const gov_action_t &r)
    {
        const auto committee_l = std::holds_alternative<gov_action_t::no_confidence_t>(l.val)
            || std::holds_alternative<gov_action_t::update_committee_t>(l.val);
        const auto committee_r = std::holds_alternative<gov_action_t::no_confidence_t>(r.val)
            || std::holds_alternative<gov_action_t::update_committee_t>(r.val);
        if (committee_l || committee_r)
            return committee_l && committee_r;
        return l.val.index() == r.val.index();
    }

    bool is_bootstrap_action(const gov_action_t &ga)
    {
        return std::holds_alternative<gov_action_t::parameter_change_t>(ga.val)
            || std::holds_alternative<gov_action_t::hard_fork_init_t>(ga.val)
            || std::holds_alternative<gov_action_t::info_action_t>(ga.val);
    }

    bool action_has_parent(const gov_action_t &ga)
    {
        return std::holds_alternative<gov_action_t::parameter_change_t>(ga.val)
            || std::holds_alternative<gov_action_t::hard_fork_init_t>(ga.val)
            || std::holds_alternative<gov_action_t::no_confidence_t>(ga.val)
            || std::holds_alternative<gov_action_t::update_committee_t>(ga.val)
            || std::holds_alternative<gov_action_t::new_constitution_t>(ga.val);
    }

    optional_gov_action_id_t action_parent(const gov_action_t &ga)
    {
        return std::visit([](const auto &a) -> optional_gov_action_id_t {
            using T = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<T, gov_action_t::parameter_change_t>
                    || std::is_same_v<T, gov_action_t::hard_fork_init_t>
                    || std::is_same_v<T, gov_action_t::no_confidence_t>
                    || std::is_same_v<T, gov_action_t::update_committee_t>
                    || std::is_same_v<T, gov_action_t::new_constitution_t>) {
                return a.prev_action_id;
            } else {
                return {};
            }
        }, ga.val);
    }

    bool prev_action_as_expected(const gov_action_t &ga, const enact_state_t &st)
    {
        return std::visit<bool>([&](const auto &a) {
            using T = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<T, gov_action_t::parameter_change_t>)
                return a.prev_action_id == st.prev_actions.param_updates;
            if constexpr (std::is_same_v<T, gov_action_t::hard_fork_init_t>)
                return a.prev_action_id == st.prev_actions.hard_forks;
            if constexpr (std::is_same_v<T, gov_action_t::no_confidence_t>)
                return a.prev_action_id == st.prev_actions.committee_updates;
            if constexpr (std::is_same_v<T, gov_action_t::update_committee_t>)
                return a.prev_action_id == st.prev_actions.committee_updates;
            if constexpr (std::is_same_v<T, gov_action_t::new_constitution_t>)
                return a.prev_action_id == st.prev_actions.constitution_updates;
            return true;
        }, ga.val);
    }

    bool committee_member_active(
        const committee_t::member_key_map &hot_keys,
        const credential_t &cold_id,
        const uint64_t expire_epoch,
        const uint64_t current_epoch)
    {
        const auto hot_it = hot_keys.find(cold_id);
        return current_epoch <= expire_epoch
            && hot_it != hot_keys.end()
            && std::holds_alternative<credential_t>(hot_it->second.val);
    }

    propose_result propose(const propose_environment &env, const proposal_t &proposal)
    {
        if (env.proposals.contains(proposal.id))
            return propose_result::fail(rule_id::gov_propose, failure::duplicate_action);
        if (proposal.procedure.deposit != env.expected_deposit)
            return propose_result::fail(rule_id::gov_propose, failure::deposit_mismatch);
        if (proposal.procedure.return_addr_network_id != env.network_id)
            return propose_result::fail(rule_id::gov_propose, failure::wrong_return_network);
        if (env.params.protocol_ver.bootstrap_phase()
                && !is_bootstrap_action(proposal.procedure.action)) {
            return propose_result::fail(
                rule_id::gov_propose,
                failure::disallowed_during_bootstrap);
        }
        if (!env.params.protocol_ver.bootstrap_phase() && !env.return_account_registered)
            return propose_result::fail(rule_id::gov_propose, failure::unregistered_return_account);
        if (!action_well_formed(
                proposal.procedure.action,
                env.params.protocol_ver,
                env.network_id)) {
            return propose_result::fail(rule_id::gov_propose, failure::action_not_well_formed);
        }
        if (!action_valid(proposal.procedure.action, env.enact_state, env.current_epoch))
            return propose_result::fail(rule_id::gov_propose, failure::action_invalid);
        if (!has_valid_parent(proposal.procedure.action, env.enact_state, env.proposals))
            return propose_result::fail(rule_id::gov_propose, failure::missing_parent);
        if (!valid_hard_fork(proposal.procedure.action, env.enact_state, env.proposals))
            return propose_result::fail(rule_id::gov_propose, failure::invalid_hard_fork);
        return propose_result::success(
            rule_id::gov_propose,
            { env.current_epoch, env.current_epoch + env.params.gov_action_lifetime });
    }

    vote_result vote(const vote_environment &env, const vote_info_t &vote)
    {
        auto gov_it = env.proposals.find(vote.action_id);
        if (gov_it == env.proposals.end())
            return vote_result::fail(rule_id::gov_vote, failure::unknown_action);
        if (env.current_epoch > gov_it->second.expires_after)
            return vote_result::fail(rule_id::gov_vote, failure::expired_action);
        if (!can_vote(gov_it->second.proposal.action, vote.voter.type, env.params.protocol_ver))
            return vote_result::fail(rule_id::gov_vote, failure::voter_not_allowed);

        switch (vote.voter.type) {
            case voter_t::type_t::const_comm_key:
            case voter_t::type_t::const_comm_script: {
                const credential_t hot_id {
                    vote.voter.hash,
                    vote.voter.type == voter_t::type_t::const_comm_script
                };
                const auto it = std::find_if(
                    env.committee_hot_keys.begin(),
                    env.committee_hot_keys.end(),
                    [&](const auto &item) {
                        return std::holds_alternative<credential_t>(item.second.val)
                            && std::get<credential_t>(item.second.val) == hot_id;
                    });
                if (it == env.committee_hot_keys.end())
                    return vote_result::fail(rule_id::gov_vote, failure::unknown_committee_voter);
                if (env.params.protocol_ver.major >= 11) {
                    if (!env.enact_state.committee)
                        return vote_result::fail(rule_id::gov_vote, failure::no_active_committee);
                    if (!env.enact_state.committee->members.contains(it->first)) {
                        return vote_result::fail(rule_id::gov_vote, failure::unknown_committee_voter);
                    }
                }
                return vote_result::success(
                    rule_id::gov_vote,
                    { &gov_it->second, vote_target::committee });
            }
            case voter_t::type_t::drep_key:
            case voter_t::type_t::drep_script: {
                const credential_t drep_id {
                    vote.voter.hash,
                    vote.voter.type == voter_t::type_t::drep_script
                };
                if (!env.dreps.contains(drep_id))
                    return vote_result::fail(rule_id::gov_vote, failure::unknown_drep);
                return vote_result::success(
                    rule_id::gov_vote,
                    { &gov_it->second, vote_target::drep });
            }
            case voter_t::type_t::pool_key:
                if (!env.pool_registered)
                    return vote_result::fail(rule_id::gov_vote, failure::unknown_pool);
                return vote_result::success(
                    rule_id::gov_vote,
                    { &gov_it->second, vote_target::pool });
            [[unlikely]] default:
                return vote_result::fail(rule_id::gov_vote, failure::unsupported_voter_type);
        }
    }
}

namespace turbo::cardano::ledger::conway {
    bool state::_has_parent(const gov_action_t &ga) const
    {
        return rules::gov::has_valid_parent(ga, _enact_state, _proposals);
    }

    bool state::_action_well_formed(const gov_action_t &ga) const
    {
        return rules::gov::action_well_formed(
            ga,
            _params.protocol_ver,
            _cfg.shelley_network_id);
    }

    bool state::_action_valid(const gov_action_t &ga, const uint64_t epoch) const
    {
        return rules::gov::action_valid(ga, _enact_state, epoch);
    }

    bool state::_valid_hf_action(const gov_action_t &ga) const
    {
        return rules::gov::valid_hard_fork(ga, _enact_state, _proposals);
    }

    bool state::_proposal_valid(const proposal_t &p, const cert_loc_t &loc) const
    {
        const auto acc_it = _accounts.find(p.procedure.return_addr);
        const rules::gov::propose_environment env {
            slot { loc.slot, _cfg }.epoch(),
            _params.gov_action_deposit,
            _cfg.shelley_network_id,
            _params,
            _enact_state,
            _proposals,
            acc_it != _accounts.end() && acc_it->second.ptr.has_value()
        };
        const auto checked = rules::gov::propose(env, p);
        if (!checked) {
            logger::warn(
                "slot: {} proposal {} failed {}: {}",
                slot { loc.slot, _cfg },
                p.id,
                rules::name(checked.rule),
                rules::gov::failure_name(checked.failure));
        }
        return static_cast<bool>(checked);
    }

    bool state::_can_vote(const gov_action_t &ga, const voter_t::type_t voter_type) const
    {
        return rules::gov::can_vote(ga, voter_type, _params.protocol_ver);
    }

    void state::process_proposal(const proposal_t &p, const cert_loc_t &loc)
    {
        const slot loc_slot { loc.slot, _cfg };
        const auto acc_it = _accounts.find(p.procedure.return_addr);
        const rules::gov::propose_environment env {
            loc_slot.epoch(),
            _params.gov_action_deposit,
            _cfg.shelley_network_id,
            _params,
            _enact_state,
            _proposals,
            acc_it != _accounts.end() && acc_it->second.ptr.has_value()
        };
        const auto checked = rules::gov::propose(env, p);
        if (!checked) [[unlikely]] {
            logger::warn(
                "slot: {} proposal {} ({}) failed {}: {}",
                loc_slot,
                p.id,
                rules::gov::action_label(p.procedure.action),
                rules::name(checked.rule),
                rules::gov::failure_name(checked.failure));
            throw error(fmt::format(
                "invalid Conway governance proposal {}: {}",
                p.id,
                rules::gov::failure_name(checked.failure)));
        }
        if (_params.protocol_ver.major < 11 && _num_dormant_epochs) {
            for (auto &[drep_id, info]: _drep_state) {
                static_cast<void>(drep_id);
                const auto new_expiry = info.expire_epoch + _num_dormant_epochs;
                if (new_expiry >= checked.effect.proposed_in)
                    info.expire_epoch = new_expiry;
            }
            _num_dormant_epochs = 0;
        }
        _proposals.try_emplace(
            p.id,
            p.procedure,
            checked.effect.proposed_in,
            checked.effect.expires_after,
            loc);
        _deposited += p.procedure.deposit;
    }

    void state::process_vote(const vote_info_t &v, const cert_loc_t &loc)
    {
        const slot loc_slot { loc.slot, _cfg };
        const auto is_pool_vote = v.voter.type == voter_t::type_t::pool_key;
        const rules::gov::vote_environment env {
            loc_slot.epoch(),
            _epoch,
            _params,
            _enact_state,
            _committee_hot_keys,
            _proposals,
            _drep_state,
            !is_pool_vote || _active_pool_params.contains(v.voter.hash)
        };
        const auto checked = rules::gov::vote(env, v);
        if (!checked) [[unlikely]] {
            throw error(fmt::format(
                "{} failed for action {} at {}: {}",
                rules::name(checked.rule),
                v.action_id,
                loc_slot,
                rules::gov::failure_name(checked.failure)));
        }
        switch (checked.effect.target) {
            case rules::gov::vote_target::committee: {
                const credential_t hot_id {
                    v.voter.hash,
                    v.voter.type == voter_t::type_t::const_comm_script
                };
                checked.effect.action->committee_votes[hot_id] = v.voting_procedure;
                break;
            }
            case rules::gov::vote_target::drep: {
                const credential_t drep_id {
                    v.voter.hash,
                    v.voter.type == voter_t::type_t::drep_script
                };
                if (_params.protocol_ver.major < 11) {
                    auto &info = _drep_state.at(drep_id);
                    info.expire_epoch = drep_info_t::compute_expire_epoch(
                        _params,
                        loc_slot.epoch(),
                        _num_dormant_epochs);
                }
                checked.effect.action->drep_votes[drep_id] = v.voting_procedure;
                break;
            }
            case rules::gov::vote_target::pool:
                checked.effect.action->pool_votes[v.voter.hash] = v.voting_procedure;
                break;
        }
    }
}
