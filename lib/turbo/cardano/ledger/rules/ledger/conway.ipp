/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Ledger.
// Included at namespace scope by lib/turbo/cardano/ledger/conway-state.cpp.

    void state::_begin_transaction(const cert_loc_t &loc)
    {
        const auto tx = std::pair<uint64_t, size_t> { loc.slot, loc.tx_idx };
        if (!_certificate_tx || *_certificate_tx != tx) {
            finish_certificates();
            _certificate_tx = tx;
        }
    }

    void state::_process_timed_update(tx_out_ref_list &collected_collateral, uint64_t &collateral_refund, timed_update_t &&upd)
    {
        _begin_transaction(upd.loc);
        std::visit([&](const auto &u) {
            using T = std::decay_t<decltype(u)>;
            if constexpr (std::is_same_v<T, reg_cert>
                || std::is_same_v<T, stake_reg_deleg_cert>
                || std::is_same_v<T, vote_reg_deleg_cert>
                || std::is_same_v<T, stake_vote_reg_deleg_cert>
                || std::is_same_v<T, reg_drep_cert>
                || std::is_same_v<T, vote_deleg_cert>
                || std::is_same_v<T, stake_vote_deleg_cert>
                || std::is_same_v<T, auth_committee_hot_cert>
                || std::is_same_v<T, resign_committee_cold_cert>
                || std::is_same_v<T, update_drep_cert>
                || std::is_same_v<T, unreg_cert>
                || std::is_same_v<T, unreg_drep_cert>) {
                _apply_cert(u, upd.loc);
            } else if constexpr (std::is_same_v<T, proposal_t>) {
                finish_certificates();
                _apply_proposal(u, upd.loc);
            } else if constexpr (std::is_same_v<T, vote_info_t>) {
                finish_certificates();
                _apply_vote(u, upd.loc);
            } else if constexpr (std::is_same_v<T, index::timed_update::conway_tx_prelude>) {
                _process_tx_prelude(u, upd.loc);
            } else if constexpr (std::is_same_v<T, index::timed_update::stake_withdraw>) {
                if (_params.protocol_ver.major < 11)
                    withdraw_reward(u.stake_id, u.amount);
            } else {
                babbage::state::_process_timed_update(collected_collateral, collateral_refund, std::move(upd));
            }
        }, upd.update);
    }

    void state::process_proposal(const proposal_t &proposal, const cert_loc_t &loc)
    {
        _begin_transaction(loc);
        finish_certificates();
        _apply_proposal(proposal, loc);
    }

    void state::process_vote(const vote_info_t &vote, const cert_loc_t &loc)
    {
        _begin_transaction(loc);
        finish_certificates();
        _apply_vote(vote, loc);
    }

    void state::finish_transaction()
    {
        finish_certificates();
        _certificate_tx.reset();
    }
