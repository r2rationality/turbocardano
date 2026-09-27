/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Certs.
// Included at namespace scope by lib/turbo/cardano/ledger/rules/certs/conway.cpp.

    void state::_register_stake(const uint64_t slot, const stake_ident &id,
        const std::optional<uint64_t> deposit, const size_t tx_idx, const size_t cert_idx)
    {
        const auto it = _accounts.find(id);
        if (it != _accounts.end() && it->second.ptr) [[unlikely]]
            throw error("stake credential is already registered");
        // Agda's reg c 0 denotes the legacy certificate without an explicit
        // deposit (Certs.cwitness). C++ represents that form with nullopt.
        if (deposit && *deposit != _params.key_deposit) [[unlikely]]
            throw error("stake registration deposit does not match keyDeposit");
        babbage::state::register_stake(slot, id, deposit, tx_idx, cert_idx);
    }

    void state::_register_delegating_stake(const stake_ident &id, const uint64_t deposit, const cert_loc_t &loc)
    {
        const auto it = _accounts.find(id);
        if (it != _accounts.end() && it->second.ptr) {
            if (deposit != 0) [[unlikely]]
                throw error("delegation of a registered credential requires zero deposit");
        } else {
            _register_stake(loc.slot, id, deposit, loc.tx_idx, loc.cert_idx);
        }
    }

    void state::_apply_cert(const stake_dereg_cert &c, const cert_loc_t &loc)
    {
        retire_stake(loc.slot, c.stake_id, {});
    }

    void state::withdraw_reward(const stake_ident &id, const uint64_t amount)
    {
        if (!has_stake(id)) [[unlikely]]
            throw error("withdrawal requires a registered reward account");
        // PRE-CERT requires input vote delegation after the PV9 bootstrap phase.
        // Historical PV9 withdrawals remain valid without a vote delegation.
        if (!_params.protocol_ver.bootstrap_phase() && !id.script && !_accounts.at(id).vote_deleg) [[unlikely]]
            throw error("key reward withdrawal requires an input vote delegation");
        babbage::state::withdraw_reward(id, amount);
    }

    void state::delegate_stake(const stake_ident &id, const pool_hash &pool)
    {
        if (!has_stake(id)) [[unlikely]]
            throw error("stake delegation requires a registered credential");
        babbage::state::delegate_stake(id, pool);
    }

    void state::_delegate_vote(const stake_ident &stake_id, const drep_t &drep)
    {
        if (!has_stake(stake_id)) [[unlikely]]
            throw error("vote delegation requires a registered credential");
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
                if (!preserve_incorrect_delegation) [[unlikely]] {
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
        if (!acc.ptr || acc.reward != 0) [[unlikely]]
            throw error("stake deregistration requires a registered credential with zero rewards");
        if (deposit && *deposit != acc.deposit) [[unlikely]]
            throw error("stake deregistration refund does not match its deposit");
        if (_deposited < acc.deposit) [[unlikely]]
            throw error("stake deregistration exceeds the deposited pot");
        const auto refund = acc.deposit;
        if (acc.vote_deleg) {
            if (std::holds_alternative<credential_t>(acc.vote_deleg->val)) {
                auto d_it = _drep_state.find(std::get<credential_t>(acc.vote_deleg->val));
                if (d_it != _drep_state.end())
                    d_it->second.delegs.erase(stake_id);
            }
            acc.vote_deleg.reset();
        }
        babbage::state::retire_stake(slot, stake_id, refund);
    }

    // DELEG-reg
    void state::_apply_cert(const reg_cert &c, const cert_loc_t &loc)
    {
        _register_stake(loc.slot, c.stake_id, c.deposit, loc.tx_idx, loc.cert_idx);
    }

    // DELEG-dereg
    void state::_apply_cert(const unreg_cert &c, const cert_loc_t &loc)
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
    void state::_apply_cert(const vote_deleg_cert &c, const cert_loc_t &)
    {
        _delegate_vote(c.stake_id, c.drep);
    }

    // DELEG-delegate
    void state::_apply_cert(const stake_vote_deleg_cert &c, const cert_loc_t &loc)
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
        _delegate_vote(c.stake_id, c.drep);
    }

    // DELEG-delegate
    void state::_apply_cert(const stake_reg_deleg_cert &c, const cert_loc_t &loc)
    {
        _register_delegating_stake(c.stake_id, c.deposit, loc);
        delegate_stake(c.stake_id, c.pool_id);
    }

    // DELEG-delegate
    void state::_apply_cert(const vote_reg_deleg_cert &c, const cert_loc_t &loc)
    {
        _register_delegating_stake(c.stake_id, c.deposit, loc);
        _delegate_vote(c.stake_id, c.drep);
    }

    // DELEG-delegate
    void state::_apply_cert(const stake_vote_reg_deleg_cert &c, const cert_loc_t &loc)
    {
        _register_delegating_stake(c.stake_id, c.deposit, loc);
        delegate_stake(c.stake_id, c.pool_id);
        _delegate_vote(c.stake_id, c.drep);
    }
