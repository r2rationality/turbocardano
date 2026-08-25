/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <algorithm>
#include <turbo/cardano/ledger/conway/gov.hpp>
#include <turbo/cardano/ledger/conway/ratify.hpp>

namespace turbo::cardano::ledger::conway::rules::ratify {
    bool valid_committee_term(
        const gov_action_t &ga,
        const protocol_params &params,
        const uint64_t current_epoch)
    {
        if (std::holds_alternative<gov_action_t::update_committee_t>(ga.val)) {
            const auto max_expire_epoch = current_epoch + params.committee_max_term_length;
            const auto &new_committee = std::get<gov_action_t::update_committee_t>(ga.val);
            for (const auto &[cred, expire_epoch]: new_committee.members_to_add) {
                static_cast<void>(cred);
                if (expire_epoch > max_expire_epoch)
                    return false;
            }
        }
        return true;
    }

    bool withdrawals_can_withdraw(const gov_action_t &ga, const enact_state_t &state)
    {
        if (std::holds_alternative<gov_action_t::treasury_withdrawals_t>(ga.val)) {
            uint64_t next = 0;
            const auto &withdrawals = std::get<gov_action_t::treasury_withdrawals_t>(ga.val);
            for (const auto &[reward_id, coin]: withdrawals.withdrawals) {
                static_cast<void>(reward_id);
                next += coin;
            }
            return next <= state.treasury;
        }
        return true;
    }

    decision accept(const bool delays_following_actions)
    {
        return {
            rule_id::ratify_accept,
            decision::kind::accept,
            delays_following_actions
        };
    }

    decision reject()
    {
        return {
            rule_id::ratify_reject,
            decision::kind::reject,
            false
        };
    }

    decision continue_()
    {
        return {
            rule_id::ratify_continue,
            decision::kind::continue_,
            false
        };
    }

    decision step(const environment &env, const gov_action_state_t &action)
    {
        const auto is_info = std::holds_alternative<gov_action_t::info_action_t>(
            action.proposal.action.val);
        const auto ratifiable = !is_info
            && env.previous_action_matches
            && env.committee_term_valid
            && !env.delayed
            && env.treasury_sufficient;
        if (ratifiable
                && env.accepted_by_committee
                && env.accepted_by_pools
                && env.accepted_by_dreps) {
            return accept(action.proposal.action.delaying());
        }
        if (env.current_epoch > action.expires_after)
            return reject();
        return continue_();
    }
}

namespace turbo::cardano::ledger::conway {
    state::voting_threshold_t state::_committee_voting_threshold(
        const committee_t::member_key_map &hot_keys,
        const enact_state_t &st,
        const gov_action_t &ga) const
    {
        voting_threshold_t threshold { voting_threshold_t::no_voting_threshold_t {} };
        if (st.committee) {
            if (st.params.protocol_ver.bootstrap_phase()
                    || st.committee->active_size(hot_keys, _epoch) >= st.params.committee_min_size) {
                threshold.val = st.committee->threshold;
            }
            return std::visit<voting_threshold_t>([&](const auto &action) {
                using T = std::decay_t<decltype(action)>;
                if constexpr (std::is_same_v<T, gov_action_t::no_confidence_t>
                        || std::is_same_v<T, gov_action_t::update_committee_t>) {
                    return voting_threshold_t { voting_threshold_t::no_voting_allowed_t {} };
                } else if constexpr (std::is_same_v<T, gov_action_t::info_action_t>) {
                    return voting_threshold_t { voting_threshold_t::no_voting_threshold_t {} };
                } else {
                    return threshold;
                }
            }, ga.val);
        }
        return threshold;
    }

    bool state::_committee_member_active(
        const committee_t::member_key_map &hot_keys,
        const credential_t &cold_id,
        const uint64_t expire_epoch) const
    {
        const auto hot_it = hot_keys.find(cold_id);
        return _epoch <= expire_epoch
            && hot_it != hot_keys.end()
            && std::holds_alternative<credential_t>(hot_it->second.val);
    }

    bool state::committee_accepted(const gov_action_state_t &ga) const
    {
        return _committee_accepted(nullptr, ga);
    }

    bool state::_committee_accepted(const gov_action_id_t *, const gov_action_state_t &ga) const
    {
        const auto &st = _ratify_state.new_state;
        const auto &hot_keys = _pulsing_data.committee_hot_keys;
        size_t yes = 0;
        size_t total = 0;
        if (st.committee) {
            // Starting with committee keys discards votes from members who later resigned.
            for (const auto &[cold_id, expire_epoch]: st.committee->members) {
                const auto hot_id_it = hot_keys.find(cold_id);
                const auto active = _committee_member_active(hot_keys, cold_id, expire_epoch);
                if (active) {
                    const auto &hot_id = std::get<credential_t>(hot_id_it->second.val);
                    const auto vote_it = ga.committee_votes.find(hot_id);
                    if (vote_it != ga.committee_votes.end()) {
                        if (vote_it->second.vote != vote_t::abstain) {
                            ++total;
                            if (vote_it->second.vote == vote_t::yes)
                                ++yes;
                        }
                    } else {
                        ++total;
                    }
                }
            }
        }
        const rational_u64 ratio { yes, std::max(total, size_t { 1 }) };
        return _check_threshold(
            _committee_voting_threshold(hot_keys, _ratify_state.new_state, ga.proposal.action),
            ratio);
    }

    state::default_vote_t state::_pool_default_vote(const pool_hash &id) const
    {
        // Defaults use the pulsing snapshot; the matching pool parameters are in mark.
        const auto params_it = _mark.pool_params.find(id);
        if (params_it == _mark.pool_params.end()) [[unlikely]] {
            logger::debug("default vote requested for an unregistered stake pool: {}", id);
            return default_vote_t::no;
        }
        const auto acc_it = _accounts.find(params_it->second.params.reward_id);
        if (acc_it != _accounts.end() && acc_it->second.vote_deleg) {
            if (std::holds_alternative<drep_t::abstain_t>(acc_it->second.vote_deleg->val))
                return default_vote_t::abstain;
            if (std::holds_alternative<drep_t::no_confidence_t>(acc_it->second.vote_deleg->val))
                return default_vote_t::no_confidence;
        }
        return default_vote_t::no;
    }

    state::voting_threshold_t state::_pool_voting_threshold(
        const enact_state_t &st,
        const gov_action_t &ga) const
    {
        const auto &params = st.params;
        const auto has_committee = st.committee.has_value();
        const auto thresholds = params.pool_voting_thresholds;
        return std::visit<voting_threshold_t>([&](const auto &action) {
            using T = std::decay_t<decltype(action)>;
            if constexpr (std::is_same_v<T, gov_action_t::no_confidence_t>)
                return voting_threshold_t { thresholds.motion_of_no_confidence };
            if constexpr (std::is_same_v<T, gov_action_t::update_committee_t>) {
                return voting_threshold_t {
                    has_committee
                        ? thresholds.committee_normal
                        : thresholds.committee_no_confidence
                };
            }
            if constexpr (std::is_same_v<T, gov_action_t::new_constitution_t>)
                return voting_threshold_t { voting_threshold_t::no_voting_allowed_t {} };
            if constexpr (std::is_same_v<T, gov_action_t::hard_fork_init_t>)
                return voting_threshold_t { thresholds.hard_fork_initiation };
            if constexpr (std::is_same_v<T, gov_action_t::parameter_change_t>) {
                if (action.update.security_group())
                    return voting_threshold_t { thresholds.security_voting_threshold };
                return voting_threshold_t { voting_threshold_t::no_voting_allowed_t {} };
            }
            if constexpr (std::is_same_v<T, gov_action_t::treasury_withdrawals_t>)
                return voting_threshold_t { voting_threshold_t::no_voting_allowed_t {} };
            if constexpr (std::is_same_v<T, gov_action_t::info_action_t>)
                return voting_threshold_t { voting_threshold_t::no_voting_threshold_t {} };
            throw error(fmt::format("unsupported governance action type: {}", typeid(T).name()));
        }, ga.val);
    }

    bool state::_check_threshold(const voting_threshold_t &threshold, const rational_u64 &ratio)
    {
        return std::visit<bool>([&](const auto &value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, rational_u64>)
                return ratio >= value;
            if constexpr (std::is_same_v<T, voting_threshold_t::no_voting_threshold_t>)
                return false;
            if constexpr (std::is_same_v<T, voting_threshold_t::no_voting_allowed_t>)
                return true;
            throw error(fmt::format("unsupported voting threshold type: {}", typeid(T).name()));
        }, threshold.val);
    }

    bool state::pools_accepted(const gov_action_state_t &ga) const
    {
        uint64_t yes = 0;
        uint64_t abstain = 0;
        for (const auto &[pool_id, stake]: _pulsing_data.pool_voting_power) {
            const auto vote_it = ga.pool_votes.find(pool_id);
            if (vote_it != ga.pool_votes.end()) {
                switch (vote_it->second.vote) {
                    case vote_t::abstain: abstain += stake; break;
                    case vote_t::no: break;
                    case vote_t::yes: yes += stake; break;
                    default:
                        throw error(fmt::format(
                            "unsupported vote value: {}",
                            static_cast<int>(vote_it->second.vote)));
                }
            } else if (std::holds_alternative<gov_action_t::hard_fork_init_t>(
                    ga.proposal.action.val)) {
                // An absent SPO hard-fork vote is ignored.
            } else if (_params.protocol_ver.bootstrap_phase()) {
                abstain += stake;
            } else {
                switch (_pool_default_vote(pool_id)) {
                    case default_vote_t::no_confidence:
                        if (std::holds_alternative<gov_action_t::no_confidence_t>(
                                ga.proposal.action.val)) {
                            yes += stake;
                        }
                        break;
                    case default_vote_t::abstain:
                        abstain += stake;
                        break;
                    default:
                        break;
                }
            }
        }
        const rational_u64 ratio {
            yes,
            std::max(
                _pulsing_data.pool_voting_power.total_stake() - abstain,
                uint64_t { 1 })
        };
        return _check_threshold(
            _pool_voting_threshold(_ratify_state.new_state, ga.proposal.action),
            ratio);
    }

    rational_u64 state::_param_update_threshold(
        const param_update_t &update,
        const drep_voting_thresholds_t &thresholds) const
    {
        rational_u64 result { 0, 1 };
        if (update.network_group() && result < thresholds.pp_network_group)
            result = thresholds.pp_network_group;
        if (update.governance_group() && result < thresholds.pp_governance_group)
            result = thresholds.pp_governance_group;
        if (update.technical_group() && result < thresholds.pp_technical_group)
            result = thresholds.pp_technical_group;
        if (update.economic_group() && result < thresholds.pp_economic_group)
            result = thresholds.pp_economic_group;
        return result;
    }

    state::voting_threshold_t state::_drep_voting_threshold(
        const enact_state_t &st,
        const gov_action_t &ga) const
    {
        const auto &params = st.params;
        const auto has_committee = st.committee.has_value();
        const auto &thresholds = params.protocol_ver.bootstrap_phase()
            ? drep_voting_thresholds_t::zero()
            : params.drep_voting_thresholds;
        return std::visit<voting_threshold_t>([&](const auto &action) {
            using T = std::decay_t<decltype(action)>;
            if constexpr (std::is_same_v<T, gov_action_t::no_confidence_t>)
                return voting_threshold_t { thresholds.motion_no_confidence };
            if constexpr (std::is_same_v<T, gov_action_t::update_committee_t>) {
                return voting_threshold_t {
                    has_committee
                        ? thresholds.committee_normal
                        : thresholds.committee_no_confidence
                };
            }
            if constexpr (std::is_same_v<T, gov_action_t::new_constitution_t>)
                return voting_threshold_t { thresholds.update_constitution };
            if constexpr (std::is_same_v<T, gov_action_t::hard_fork_init_t>)
                return voting_threshold_t { thresholds.hard_fork_initiation };
            if constexpr (std::is_same_v<T, gov_action_t::parameter_change_t>)
                return voting_threshold_t { _param_update_threshold(action.update, thresholds) };
            if constexpr (std::is_same_v<T, gov_action_t::treasury_withdrawals_t>)
                return voting_threshold_t { thresholds.treasury_withdrawal };
            if constexpr (std::is_same_v<T, gov_action_t::info_action_t>)
                return voting_threshold_t { voting_threshold_t::no_voting_threshold_t {} };
            throw error(fmt::format("unsupported governance action type: {}", typeid(T).name()));
        }, ga.val);
    }

    bool state::dreps_accepted(const gov_action_state_t &ga) const
    {
        uint64_t yes = 0;
        uint64_t total_without_abstain = 0;
        for (const auto &[drep, stake]: _pulsing_data.drep_voting_power) {
            std::visit([&](const auto &credential) {
                using T = std::decay_t<decltype(credential)>;
                if constexpr (std::is_same_v<T, drep_t::abstain_t>) {
                    return;
                } else if constexpr (std::is_same_v<T, drep_t::no_confidence_t>) {
                    total_without_abstain += stake;
                    if (std::holds_alternative<gov_action_t::no_confidence_t>(
                            ga.proposal.action.val)) {
                        yes += stake;
                    }
                } else if constexpr (std::is_same_v<T, credential_t>) {
                    const auto drep_it = _pulsing_data.drep_state.find(credential);
                    if (drep_it != _pulsing_data.drep_state.end()
                            && _epoch <= drep_it->second.expire_epoch) {
                        const auto vote_it = ga.drep_votes.find(credential);
                        if (vote_it != ga.drep_votes.end()) {
                            switch (vote_it->second.vote) {
                                case vote_t::no:
                                    total_without_abstain += stake;
                                    break;
                                case vote_t::yes:
                                    yes += stake;
                                    total_without_abstain += stake;
                                    break;
                                case vote_t::abstain:
                                    break;
                                default:
                                    throw error(fmt::format(
                                        "unsupported vote value: {}",
                                        static_cast<int>(vote_it->second.vote)));
                            }
                        } else {
                            total_without_abstain += stake;
                        }
                    }
                }
            }, drep.val);
        }
        const rational_u64 ratio {
            yes,
            std::max(total_without_abstain, uint64_t { 1 })
        };
        return _check_threshold(
            _drep_voting_threshold(_ratify_state.new_state, ga.proposal.action),
            ratio);
    }

    bool state::accepted_by_everyone(
        const gov_action_id_t &,
        const gov_action_state_t &action) const
    {
        // Bitwise combination intentionally evaluates all three groups so coverage
        // and diagnostics retain one observation per formal premise.
        return static_cast<int>(_committee_accepted(nullptr, action))
            & static_cast<int>(pools_accepted(action))
            & static_cast<int>(dreps_accepted(action));
    }

    void state::_gov_finalize()
    {
        _ratify_state.new_state.treasury = _treasury;
        for (const auto &[gid, action]: _pulsing_data.proposals) {
            if (_epoch <= action.proposed_in)
                continue;
            const rules::ratify::environment env {
                _epoch,
                rules::gov::prev_action_as_expected(
                    action.proposal.action,
                    _ratify_state.new_state),
                rules::ratify::valid_committee_term(
                    action.proposal.action,
                    _ratify_state.new_state.params,
                    _epoch),
                _ratify_state.delayed,
                rules::ratify::withdrawals_can_withdraw(
                    action.proposal.action,
                    _ratify_state.new_state),
                _committee_accepted(&gid, action),
                pools_accepted(action),
                dreps_accepted(action)
            };
            const auto decision = rules::ratify::step(env, action);
            switch (decision.outcome) {
                case rules::ratify::decision::kind::accept:
                    _enact_proposal(_ratify_state.new_state, gid, action.proposal.action);
                    _ratify_state.enacted.emplace_back(gid, action);
                    _ratify_state.delayed = decision.delays_following_actions;
                    break;
                case rules::ratify::decision::kind::reject:
                    _ratify_state.expired.emplace(gid);
                    break;
                case rules::ratify::decision::kind::continue_:
                    break;
            }
        }
        _ratify_state.new_state.treasury = 0;
        _ratify_ready = true;
    }
}
