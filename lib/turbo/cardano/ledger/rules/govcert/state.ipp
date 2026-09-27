/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Certs.
// Included at namespace scope by lib/turbo/cardano/ledger/rules/certs/conway.cpp.

    bool state::_known_committee_cold_id(const credential_t &cold_id) const
    {
        if (_enact_state.committee && _enact_state.committee->members.contains(cold_id))
            return true;
        for (const auto &[gid, gas]: _proposals) {
            static_cast<void>(gid);
            if (std::holds_alternative<gov_action_t::update_committee_t>(gas.proposal.action.val)) {
                const auto &upd = std::get<gov_action_t::update_committee_t>(gas.proposal.action.val);
                if (upd.members_to_add.contains(cold_id))
                    return true;
            }
        }
        return false;
    }

    // GOVCERT-ccreghot
    void state::_apply_cert(const auth_committee_hot_cert &c, const cert_loc_t &)
    {
        const auto known_cold_id = _known_committee_cold_id(c.cold_id);
        const auto current_it = _committee_hot_keys.find(c.cold_id);
        const auto checked = rules::govcert::authorize_hot(
            known_cold_id,
            current_it == _committee_hot_keys.end() ? nullptr : &current_it->second);
        if (!checked) [[unlikely]] {
            if (checked.failure == rules::govcert::failure::committee_member_resigned) [[unlikely]] {
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

    // GOVCERT-ccreghot
    void state::_apply_cert(const resign_committee_cold_cert &c, const cert_loc_t &)
    {
        const auto key_it = _committee_hot_keys.find(c.cold_id);
        const auto checked = rules::govcert::resign_cold(
            _known_committee_cold_id(c.cold_id),
            key_it == _committee_hot_keys.end() ? nullptr : &key_it->second);
        if (!checked) [[unlikely]]
            throw error(fmt::format("ineligible or already resigned committee cold_id: {}", c.cold_id));
        committee_t::resigned_t resigned {};
        if (c.anchor)
            resigned.anchor.emplace(*c.anchor);
        _committee_hot_keys[c.cold_id].val = std::move(resigned);
    }

    // GOVCERT-regdrep
    void state::_apply_cert(const reg_drep_cert &c, const cert_loc_t &loc)
    {
        const slot loc_slot { loc.slot, _cfg };
        const auto checked = rules::govcert::register_drep(
            _params,
            loc_slot.epoch(),
            _num_dormant_epochs,
            _drep_state.contains(c.drep_id),
            c.deposit);
        if (!checked) [[unlikely]] {
            if (checked.failure == rules::govcert::failure::drep_already_registered) [[unlikely]]
                throw error(fmt::format("drep already registered: {}", c.drep_id));
            throw error(fmt::format(
                "reg_drep_cert: expected deposit {} got {}",
                _params.drep_deposit,
                c.deposit));
        }
        auto &registered = _drep_state.try_emplace(c.drep_id, c.deposit, c.anchor, checked.effect.expire_epoch).first->second;
        if (auto pending = _pending_drep_removals.find(c.drep_id); pending != _pending_drep_removals.end()) {
            registered.delegs = std::move(pending->second);
            // Certificates between deregistration and re-registration may have
            // explicitly changed or removed some delegations.
            for (auto it = registered.delegs.begin(); it != registered.delegs.end();) {
                const auto acc = _accounts.find(*it);
                if (acc == _accounts.end() || !acc->second.vote_deleg
                        || !std::holds_alternative<credential_t>(acc->second.vote_deleg->val)
                        || std::get<credential_t>(acc->second.vote_deleg->val) != c.drep_id)
                    it = registered.delegs.erase(it);
                else
                    ++it;
            }
            _pending_drep_removals.erase(pending);
        }
        _deposited += c.deposit;
    }

    // GOVCERT-deregdrep
    void state::_apply_cert(const unreg_drep_cert &c, const cert_loc_t &)
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
                [[unlikely]] default:
                    throw error("unexpected GOVCERT-deregdrep failure");
            }
        }
        // POST-CERT and rmOrphanDRepVotes use the final certificate state.
        _pending_drep_removals[c.drep_id] = std::move(it->second.delegs);
        _deposited -= c.deposit;
        _drep_state.erase(it);
    }

    void state::finish_certificates()
    {
        if (_pending_drep_removals.empty())
            return;
        for (const auto &[drep, delegators]: _pending_drep_removals) {
            for (const auto &id: delegators) {
                const auto it = _accounts.find(id);
                if (it != _accounts.end() && it->second.vote_deleg
                        && std::holds_alternative<credential_t>(it->second.vote_deleg->val)
                        && std::get<credential_t>(it->second.vote_deleg->val) == drep)
                    it->second.vote_deleg.reset();
            }
        }
        // Scan proposals once per transaction, not once per deregistration.
        for (auto &[id, proposal]: _proposals) {
            static_cast<void>(id);
            for (const auto &[drep, delegators]: _pending_drep_removals) {
                static_cast<void>(delegators);
                proposal.drep_votes.erase(drep);
            }
        }
        _pending_drep_removals.clear();
    }

    // GOVCERT-regdrep
    void state::_apply_cert(const update_drep_cert &c, const cert_loc_t &loc)
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
