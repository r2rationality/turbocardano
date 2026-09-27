/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Certs.
// Included at namespace scope by lib/turbo/cardano/ledger/conway-state.cpp.

    void state::process_cert(const cert_t &cert, const cert_loc_t &loc)
    {
        _begin_transaction(loc);
        _tick(loc.slot);
        std::visit([&](const auto &c) {
            _apply_cert(c, loc);
        }, cert.val);
    }

    void state::_process_tx_prelude(
        const index::timed_update::conway_tx_prelude &prelude,
        const cert_loc_t &loc)
    {
        if (_params.protocol_ver.major < 11)
            return;

        const auto current_epoch = slot { loc.slot, _cfg }.epoch();
        if (prelude.has_proposals && _num_dormant_epochs) {
            for (auto &[drep_id, info]: _drep_state) {
                static_cast<void>(drep_id);
                const auto actual_expiry = info.expire_epoch + _num_dormant_epochs;
                if (actual_expiry >= current_epoch)
                    info.expire_epoch = actual_expiry;
            }
            _num_dormant_epochs = 0;
        }
        for (const auto &drep_id: prelude.voting_dreps) {
            if (auto it = _drep_state.find(drep_id); it != _drep_state.end()) {
                it->second.expire_epoch = drep_info_t::compute_expire_epoch(
                    _params,
                    current_epoch,
                    _num_dormant_epochs);
            }
        }
        for (const auto &withdrawal: prelude.withdrawals)
            _withdraw_reward(withdrawal.reward_id, withdrawal.amount);
    }

    void state::process_cert(const stake_dereg_cert &c, const cert_loc_t &loc)
    {
        _begin_transaction(loc);
        _apply_cert(c, loc);
    }

    void state::process_cert(const reg_cert &c, const cert_loc_t &loc)
    {
        _begin_transaction(loc);
        _apply_cert(c, loc);
    }

    void state::process_cert(const unreg_cert &c, const cert_loc_t &loc)
    {
        _begin_transaction(loc);
        _apply_cert(c, loc);
    }

    void state::process_cert(const vote_deleg_cert &c, const cert_loc_t &loc)
    {
        _begin_transaction(loc);
        _apply_cert(c, loc);
    }

    void state::process_cert(const stake_vote_deleg_cert &c, const cert_loc_t &loc)
    {
        _begin_transaction(loc);
        _apply_cert(c, loc);
    }

    void state::process_cert(const stake_reg_deleg_cert &c, const cert_loc_t &loc)
    {
        _begin_transaction(loc);
        _apply_cert(c, loc);
    }

    void state::process_cert(const vote_reg_deleg_cert &c, const cert_loc_t &loc)
    {
        _begin_transaction(loc);
        _apply_cert(c, loc);
    }

    void state::process_cert(const stake_vote_reg_deleg_cert &c, const cert_loc_t &loc)
    {
        _begin_transaction(loc);
        _apply_cert(c, loc);
    }

    void state::process_cert(const auth_committee_hot_cert &c, const cert_loc_t &loc)
    {
        _begin_transaction(loc);
        _apply_cert(c, loc);
    }

    void state::process_cert(const resign_committee_cold_cert &c, const cert_loc_t &loc)
    {
        _begin_transaction(loc);
        _apply_cert(c, loc);
    }

    void state::process_cert(const reg_drep_cert &c, const cert_loc_t &loc)
    {
        _begin_transaction(loc);
        _apply_cert(c, loc);
    }

    void state::process_cert(const unreg_drep_cert &c, const cert_loc_t &loc)
    {
        _begin_transaction(loc);
        _apply_cert(c, loc);
    }

    void state::process_cert(const update_drep_cert &c, const cert_loc_t &loc)
    {
        _begin_transaction(loc);
        _apply_cert(c, loc);
    }

    void state::register_stake(uint64_t slot, const stake_ident &id,
        std::optional<uint64_t> deposit, size_t tx_idx, size_t cert_idx)
    {
        _begin_transaction({ slot, tx_idx, cert_idx });
        _register_stake(slot, id, deposit, tx_idx, cert_idx);
    }

    void state::delegate_vote(const stake_ident &id, const drep_t &drep, const cert_loc_t &loc)
    {
        _begin_transaction(loc);
        _delegate_vote(id, drep);
    }
