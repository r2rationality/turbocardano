/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/ledger/conway/enact.hpp>

namespace turbo::cardano::ledger::conway::rules::enact {
    result no_confidence(
        enact_state_t &state,
        const gov_action_id_t &gid,
        const gov_action_t::no_confidence_t &)
    {
        state.committee.reset();
        state.prev_actions.committee_updates = gid;
        return result::success(rule_id::enact_no_conf);
    }

    result update_committee(
        enact_state_t &state,
        const gov_action_id_t &gid,
        const gov_action_t::update_committee_t &action,
        const uint64_t current_epoch)
    {
        const auto max_term = current_epoch + state.params.committee_max_term_length;
        for (const auto &[credential, expire_epoch]: action.members_to_add) {
            static_cast<void>(credential);
            if (expire_epoch > max_term) {
                return result::fail(
                    rule_id::enact_upd_comm,
                    failure::committee_term_too_long);
            }
        }
        if (!state.committee)
            state.committee.emplace();
        for (const auto &cred: action.members_to_remove)
            state.committee->members.erase(cred);
        for (const auto &[cred, expire_epoch]: action.members_to_add)
            state.committee->members[cred] = expire_epoch;
        state.committee->threshold = action.new_threshold;
        state.prev_actions.committee_updates = gid;
        return result::success(rule_id::enact_upd_comm);
    }

    result new_constitution(
        enact_state_t &state,
        const gov_action_id_t &gid,
        const gov_action_t::new_constitution_t &action)
    {
        state.constitution = action.new_constitution;
        state.prev_actions.constitution_updates = gid;
        return result::success(rule_id::enact_new_const);
    }

    result hard_fork(
        enact_state_t &state,
        const gov_action_id_t &gid,
        const gov_action_t::hard_fork_init_t &action)
    {
        logger::info("enacting new protocol version: {}", action.protocol_ver);
        state.params.protocol_ver = action.protocol_ver;
        state.prev_actions.hard_forks = gid;
        return result::success(rule_id::enact_hf);
    }

    result parameter_change(
        enact_state_t &state,
        const gov_action_id_t &gid,
        const gov_action_t::parameter_change_t &action)
    {
        logger::info("enacting new protocol parameters: {}", action.update);
        state.params.apply(action.update);
        state.prev_actions.param_updates = gid;
        return result::success(rule_id::enact_pparams);
    }

    result treasury_withdrawal(
        enact_state_t &state,
        const gov_action_id_t &,
        const gov_action_t::treasury_withdrawals_t &action)
    {
        uint64_t total = 0;
        for (const auto &[reward_id, coin]: action.withdrawals) {
            static_cast<void>(reward_id);
            total += coin;
        }
        if (total > state.treasury) [[unlikely]]
            return result::fail(rule_id::enact_wdrl, failure::treasury_insufficient);
        for (const auto &[reward_id, coin]: action.withdrawals)
            state.withdrawals[address { reward_id }.stake_id()] += coin;
        state.treasury -= total;
        return result::success(rule_id::enact_wdrl);
    }

    result info(
        enact_state_t &,
        const gov_action_id_t &,
        const gov_action_t::info_action_t &)
    {
        return result::success(rule_id::enact_info);
    }

    result step(
        enact_state_t &state,
        const gov_action_id_t &gid,
        const gov_action_t &action,
        const uint64_t current_epoch)
    {
        return std::visit([&](const auto &a) {
            using T = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<T, gov_action_t::no_confidence_t>) {
                return no_confidence(state, gid, a);
            } else if constexpr (std::is_same_v<T, gov_action_t::update_committee_t>) {
                return update_committee(state, gid, a, current_epoch);
            } else if constexpr (std::is_same_v<T, gov_action_t::new_constitution_t>) {
                return new_constitution(state, gid, a);
            } else if constexpr (std::is_same_v<T, gov_action_t::hard_fork_init_t>) {
                return hard_fork(state, gid, a);
            } else if constexpr (std::is_same_v<T, gov_action_t::parameter_change_t>) {
                return parameter_change(state, gid, a);
            } else if constexpr (std::is_same_v<T, gov_action_t::treasury_withdrawals_t>) {
                return treasury_withdrawal(state, gid, a);
            } else {
                static_assert(std::is_same_v<T, gov_action_t::info_action_t>);
                return info(state, gid, a);
            }
        }, action.val);
    }
}

namespace turbo::cardano::ledger::conway {
    void state::_enact_proposal(
        enact_state_t &st,
        const gov_action_id_t &gid,
        const gov_action_t &ga)
    {
        const auto enacted = rules::enact::step(st, gid, ga, _epoch);
        if (!enacted) [[unlikely]] {
            if (enacted.failure == rules::enact::failure::committee_term_too_long) {
                throw error(fmt::format(
                    "{} failed: committee term exceeds the maximum",
                    rules::name(enacted.rule)));
            }
            throw error(fmt::format(
                "{} failed: treasury withdrawal exceeds the remaining treasury",
                rules::name(enacted.rule)));
        }
    }
}
