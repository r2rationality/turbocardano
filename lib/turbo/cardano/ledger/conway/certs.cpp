/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/ledger/conway/certs.hpp>
#include <turbo/cardano/ledger/conway/detail.hpp>

namespace turbo::cardano::ledger::conway::rules::govcert {
    expiry_result register_drep(
        const protocol_params &params,
        const uint64_t current_epoch,
        const uint64_t dormant_epochs,
        const bool already_registered,
        const uint64_t deposit)
    {
        if (already_registered)
            return expiry_result::fail(rule_id::govcert_regdrep, failure::drep_already_registered);
        if (deposit != params.drep_deposit)
            return expiry_result::fail(rule_id::govcert_regdrep, failure::deposit_mismatch);
        return expiry_result::success(
            rule_id::govcert_regdrep,
            { drep_info_t::compute_reg_expire_epoch(params, current_epoch, dormant_epochs) });
    }

    result deregister_drep(
        const bool registered,
        const uint64_t registered_deposit,
        const uint64_t requested_deposit,
        const uint64_t deposited_pot)
    {
        if (!registered)
            return result::fail(rule_id::govcert_deregdrep, failure::drep_unknown);
        if (registered_deposit != requested_deposit)
            return result::fail(rule_id::govcert_deregdrep, failure::deposit_mismatch);
        if (deposited_pot < requested_deposit)
            return result::fail(rule_id::govcert_deregdrep, failure::deposited_pot_insufficient);
        return result::success(rule_id::govcert_deregdrep);
    }

    expiry_result update_drep(
        const protocol_params &params,
        const uint64_t current_epoch,
        const uint64_t dormant_epochs,
        const bool registered)
    {
        if (!registered)
            return expiry_result::fail(rule_id::govcert_update_drep, failure::drep_unknown);
        return expiry_result::success(
            rule_id::govcert_update_drep,
            { drep_info_t::compute_expire_epoch(params, current_epoch, dormant_epochs) });
    }

    result authorize_hot(const bool known_cold_id, const committee_t::hot_key_t *current_key)
    {
        if (!known_cold_id)
            return result::fail(rule_id::govcert_ccreghot, failure::committee_member_unknown);
        if (current_key && std::holds_alternative<committee_t::resigned_t>(current_key->val))
            return result::fail(rule_id::govcert_ccreghot, failure::committee_member_resigned);
        return result::success(rule_id::govcert_ccreghot);
    }

    result resign_cold(const bool committee_exists, const bool cold_id_known)
    {
        if (committee_exists && !cold_id_known)
            return result::fail(rule_id::govcert_resign_cold, failure::committee_member_unknown);
        return result::success(rule_id::govcert_resign_cold);
    }
}

namespace turbo::cardano::ledger::conway::rules::certs {
    rule_id transition_rule(const cert_t &certificate)
    {
        return std::visit([](const auto &signal) {
            using T = std::decay_t<decltype(signal)>;
            if constexpr (std::is_same_v<T, pool_reg_cert>
                    || std::is_same_v<T, pool_retire_cert>) {
                return rule_id::cert_pool;
            } else if constexpr (std::is_same_v<T, genesis_deleg_cert>
                    || std::is_same_v<T, instant_reward_cert>) {
                return rule_id::unsupported_certificate;
            } else if constexpr (std::is_same_v<T, auth_committee_hot_cert>
                    || std::is_same_v<T, resign_committee_cold_cert>
                    || std::is_same_v<T, reg_drep_cert>
                    || std::is_same_v<T, unreg_drep_cert>
                    || std::is_same_v<T, update_drep_cert>) {
                return rule_id::cert_vdel;
            } else {
                return rule_id::cert_deleg;
            }
        }, certificate.val);
    }

    rule_id constructor_rule(const cert_t &certificate)
    {
        return std::visit([](const auto &signal) {
            using T = std::decay_t<decltype(signal)>;
            if constexpr (std::is_same_v<T, pool_reg_cert>) {
                return rule_id::pool_regpool;
            } else if constexpr (std::is_same_v<T, pool_retire_cert>) {
                return rule_id::pool_retirepool;
            } else if constexpr (std::is_same_v<T, auth_committee_hot_cert>
                    || std::is_same_v<T, resign_committee_cold_cert>) {
                return rule_id::govcert_ccreghot;
            } else if constexpr (std::is_same_v<T, reg_drep_cert>
                    || std::is_same_v<T, update_drep_cert>) {
                return rule_id::govcert_regdrep;
            } else if constexpr (std::is_same_v<T, unreg_drep_cert>) {
                return rule_id::govcert_deregdrep;
            } else if constexpr (std::is_same_v<T, reg_cert>
                    || std::is_same_v<T, stake_reg_cert>) {
                return rule_id::deleg_reg;
            } else if constexpr (std::is_same_v<T, unreg_cert>
                    || std::is_same_v<T, stake_dereg_cert>) {
                return rule_id::deleg_dereg;
            } else if constexpr (std::is_same_v<T, stake_deleg_cert>
                    || std::is_same_v<T, vote_deleg_cert>
                    || std::is_same_v<T, stake_vote_deleg_cert>
                    || std::is_same_v<T, stake_reg_deleg_cert>
                    || std::is_same_v<T, vote_reg_deleg_cert>
                    || std::is_same_v<T, stake_vote_reg_deleg_cert>) {
                return rule_id::deleg_delegate;
            } else {
                return rule_id::unsupported_certificate;
            }
        }, certificate.val);
    }
}

namespace turbo::cardano::ledger::conway {
    void state::delegate_vote(const stake_ident &stake_id, const drep_t &drep, const cert_loc_t &)
    {
        const auto preserve_incorrect_delegation = _params.protocol_ver.bootstrap_phase();
        auto new_drep_it = _drep_state.end();
        if (std::holds_alternative<credential_t>(drep.val))
            new_drep_it = _drep_state.find(std::get<credential_t>(drep.val));
        auto &acc = detail::map_nice_at(_accounts, stake_id);
        if (acc.vote_deleg && std::holds_alternative<credential_t>(acc.vote_deleg->val)) {
            const auto &old_cred = std::get<credential_t>(acc.vote_deleg->val);
            auto old_drep_it = _drep_state.find(old_cred);
            if (old_drep_it != _drep_state.end()
                    && (!preserve_incorrect_delegation || new_drep_it == _drep_state.end())) {
                old_drep_it->second.delegs.erase(stake_id);
            }
        }
        // Re-delegation can target the same DRep, so insert after removing the old link.
        if (std::holds_alternative<credential_t>(drep.val)) {
            if (new_drep_it == _drep_state.end()) [[unlikely]] {
                if (!preserve_incorrect_delegation) {
                    throw error(fmt::format(
                        "delegate_vote: {} delegating to an unknown drep credential: {}",
                        stake_id,
                        std::get<credential_t>(drep.val)));
                }
                logger::debug(
                    "delegate_vote: {} to an unknown DRep {} - ignoring in protocol ver: {} ",
                    stake_id,
                    std::get<credential_t>(drep.val),
                    _params.protocol_ver);
            } else {
                new_drep_it->second.delegs.emplace(stake_id);
            }
        }
        acc.vote_deleg = drep;
    }

    void state::retire_stake(
        const uint64_t slot,
        const stake_ident &stake_id,
        const std::optional<uint64_t> deposit)
    {
        auto &acc = detail::map_nice_at(_accounts, stake_id);
        if (acc.vote_deleg) {
            if (std::holds_alternative<credential_t>(acc.vote_deleg->val)) {
                auto d_it = _drep_state.find(std::get<credential_t>(acc.vote_deleg->val));
                if (d_it != _drep_state.end())
                    d_it->second.delegs.erase(stake_id);
            }
            acc.vote_deleg.reset();
        }
        babbage::state::retire_stake(slot, stake_id, deposit);
    }

    // DELEG-reg
    void state::process_cert(const reg_cert &c, const cert_loc_t &loc)
    {
        register_stake(loc.slot, c.stake_id, c.deposit, loc.tx_idx, loc.cert_idx);
    }

    // DELEG-dereg
    void state::process_cert(const unreg_cert &c, const cert_loc_t &loc)
    {
        logger::trace(
            "conway unreg_cert stake_id: {} slot: {} tx_idx: {} cert_idx: {} deposit: {}",
            c.stake_id,
            cardano::slot { loc.slot, _cfg },
            loc.tx_idx,
            loc.cert_idx,
            cardano::amount { c.deposit });
        retire_stake(loc.slot, c.stake_id, c.deposit);
    }

    // DELEG-delegate
    void state::process_cert(const vote_deleg_cert &c, const cert_loc_t &loc)
    {
        delegate_vote(c.stake_id, c.drep, loc);
    }

    // DELEG-delegate
    void state::process_cert(const stake_vote_deleg_cert &c, const cert_loc_t &loc)
    {
        const auto acc_it = _accounts.find(c.stake_id);
        if (acc_it == _accounts.end() || !acc_it->second.ptr) [[unlikely]] {
            logger::debug(
                "slot: {} conway stake_vote_deleg_cert stake_id: {} pool_id: {} drep: {} account_known: {} registered: {} reward: {} deposit: {} delegated: {} vote_delegated: {} tx_idx: {} cert_idx: {}",
                cardano::slot { loc.slot, _cfg },
                c.stake_id,
                c.pool_id,
                c.drep,
                acc_it != _accounts.end(),
                acc_it != _accounts.end() && acc_it->second.ptr.has_value(),
                cardano::amount { acc_it != _accounts.end() ? acc_it->second.reward : 0 },
                cardano::amount { acc_it != _accounts.end() ? acc_it->second.deposit : 0 },
                acc_it != _accounts.end() && acc_it->second.deleg.has_value(),
                acc_it != _accounts.end() && acc_it->second.vote_deleg.has_value(),
                loc.tx_idx,
                loc.cert_idx);
        }
        delegate_stake(c.stake_id, c.pool_id);
        delegate_vote(c.stake_id, c.drep, loc);
    }

    // DELEG-delegate
    void state::process_cert(const stake_reg_deleg_cert &c, const cert_loc_t &loc)
    {
        register_stake(loc.slot, c.stake_id, c.deposit, loc.tx_idx, loc.cert_idx);
        delegate_stake(c.stake_id, c.pool_id);
    }

    // DELEG-delegate
    void state::process_cert(const vote_reg_deleg_cert &c, const cert_loc_t &loc)
    {
        register_stake(loc.slot, c.stake_id, c.deposit, loc.tx_idx, loc.cert_idx);
        delegate_vote(c.stake_id, c.drep, loc);
    }

    // DELEG-delegate
    void state::process_cert(const stake_vote_reg_deleg_cert &c, const cert_loc_t &loc)
    {
        register_stake(loc.slot, c.stake_id, c.deposit, loc.tx_idx, loc.cert_idx);
        delegate_stake(c.stake_id, c.pool_id);
        delegate_vote(c.stake_id, c.drep, loc);
    }

    // GOVCERT-ccreghot
    void state::process_cert(const auth_committee_hot_cert &c, const cert_loc_t &)
    {
        const auto known_cold_id = [&] {
            if (_enact_state.committee && _enact_state.committee->members.contains(c.cold_id))
                return true;
            for (const auto &[gid, gas]: _proposals) {
                static_cast<void>(gid);
                if (std::holds_alternative<gov_action_t::update_committee_t>(gas.proposal.action.val)) {
                    const auto &upd = std::get<gov_action_t::update_committee_t>(gas.proposal.action.val);
                    if (upd.members_to_add.contains(c.cold_id))
                        return true;
                }
            }
            return false;
        }();
        const auto current_it = _committee_hot_keys.find(c.cold_id);
        const auto checked = rules::govcert::authorize_hot(
            known_cold_id,
            current_it == _committee_hot_keys.end() ? nullptr : &current_it->second);
        if (!checked) [[unlikely]] {
            if (checked.failure == rules::govcert::failure::committee_member_resigned) {
                throw error(fmt::format(
                    "an attempt to provide a hot certificate to a resigned committee member: {}",
                    c.cold_id));
            }
            throw error(fmt::format(
                "an attempt to provide a hot certificate to an unknown committee cold_id: {}",
                c.cold_id));
        }
        const auto [it, created] = _committee_hot_keys.try_emplace(c.cold_id, c.hot_id);
        if (!created)
            it->second.val = c.hot_id;
    }

    void state::process_cert(const resign_committee_cold_cert &c, const cert_loc_t &)
    {
        const auto key_it = _committee_hot_keys.find(c.cold_id);
        const auto checked = rules::govcert::resign_cold(
            _enact_state.committee.has_value(),
            key_it != _committee_hot_keys.end());
        if (!checked) [[unlikely]]
            throw error(fmt::format("an unknown resigning committee cold_id: {}", c.cold_id));
        if (_enact_state.committee) {
            committee_t::resigned_t resigned {};
            if (c.anchor)
                resigned.anchor.emplace(*c.anchor);
            key_it->second.val = std::move(resigned);
        }
    }

    // GOVCERT-regdrep
    void state::process_cert(const reg_drep_cert &c, const cert_loc_t &loc)
    {
        const slot loc_slot { loc.slot, _cfg };
        const auto checked = rules::govcert::register_drep(
            _params,
            loc_slot.epoch(),
            _num_dormant_epochs,
            _drep_state.contains(c.drep_id),
            c.deposit);
        if (!checked) [[unlikely]] {
            if (checked.failure == rules::govcert::failure::drep_already_registered)
                throw error(fmt::format("drep already registered: {}", c.drep_id));
            throw error(fmt::format(
                "reg_drep_cert: expected deposit {} got {}",
                _params.drep_deposit,
                c.deposit));
        }
        _drep_state.try_emplace(c.drep_id, c.deposit, c.anchor, checked.effect.expire_epoch);
        _deposited += c.deposit;
    }

    // GOVCERT-deregdrep
    void state::process_cert(const unreg_drep_cert &c, const cert_loc_t &)
    {
        const auto it = _drep_state.find(c.drep_id);
        const auto checked = rules::govcert::deregister_drep(
            it != _drep_state.end(),
            it == _drep_state.end() ? 0 : it->second.deposited,
            c.deposit,
            _deposited);
        if (!checked) [[unlikely]] {
            switch (checked.failure) {
                case rules::govcert::failure::drep_unknown:
                    throw error(fmt::format("unreg_drep_cert: an unknown drep_id: {}", c.drep_id));
                case rules::govcert::failure::deposit_mismatch:
                    throw error(fmt::format(
                        "the registered drep deposit: {} does not match the requested withdrawal: {}",
                        it->second.deposited,
                        c.deposit));
                case rules::govcert::failure::deposited_pot_insufficient:
                    throw error(fmt::format("unable to withdraw the old drep deposit: {}", it->second.deposited));
                default:
                    throw error("unexpected GOVCERT-deregdrep failure");
            }
        }
        // Protocol version 9.0 may retain delegates that have already re-delegated.
        for (const auto &deleg_id: it->second.delegs) {
            auto acc_it = _accounts.find(deleg_id);
            if (acc_it != _accounts.end() && acc_it->second.vote_deleg)
                acc_it->second.vote_deleg.reset();
        }
        for (auto &[id, ga_st]: _proposals) {
            static_cast<void>(id);
            ga_st.drep_votes.erase(c.drep_id);
        }
        _deposited -= c.deposit;
        _drep_state.erase(it);
    }

    void state::process_cert(const update_drep_cert &c, const cert_loc_t &loc)
    {
        const slot loc_slot { loc.slot, _cfg };
        const auto drep_it = _drep_state.find(c.drep_id);
        const auto checked = rules::govcert::update_drep(
            _params,
            loc_slot.epoch(),
            _num_dormant_epochs,
            drep_it != _drep_state.end());
        if (!checked) [[unlikely]]
            throw error(fmt::format("update_drep_cert: an unknown drep_id: {}", c.drep_id));
        drep_it->second.anchor = c.anchor;
        drep_it->second.expire_epoch = checked.effect.expire_epoch;
        ++drep_it->second.num_updates;
    }
}
