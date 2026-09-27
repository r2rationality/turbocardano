/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Ledger.
// Included at namespace scope by lib/turbo/cardano/ledger/shelley.cpp.

    void state::_process_timed_update(tx_out_ref_list &collected_collateral, uint64_t &collateral_refund, timed_update_t &&upd)
    {
        std::visit([&](const auto &u) {
            using T = std::decay_t<decltype(u)>;
            if constexpr (std::is_same_v<T, index::timed_update::stake_withdraw>) {
                withdraw_reward(u.stake_id, u.amount);
            } else if constexpr (std::is_same_v<T, param_update_proposal>) {
                propose_update(upd.loc.slot, u);
            } else if constexpr (std::is_same_v<T, param_update_vote>) {
                proposal_vote(upd.loc.slot, u);
            } else if constexpr (std::is_same_v<T, index::timed_update::collected_collateral_input>) {
                collected_collateral.emplace_back(u.tx_hash, u.txo_idx);
            } else if constexpr (std::is_same_v<T, index::timed_update::collected_collateral_refund>) {
                collateral_refund += u.refund;
            } else if constexpr (std::is_same_v<T, stake_reg_cert>
                || std::is_same_v<T, stake_dereg_cert>
                || std::is_same_v<T, stake_deleg_cert>
                || std::is_same_v<T, pool_reg_cert>
                || std::is_same_v<T, pool_retire_cert>
                || std::is_same_v<T, genesis_deleg_cert>
                || std::is_same_v<T, instant_reward_cert>) {
                process_cert(cert_t { u }, upd.loc);
            } else {
                throw error(fmt::format("unsupported timed update: {}", typeid(u).name()));
            }
        }, upd.update);
    }

    std::pair<tx_out_ref_list, uint64_t> state::_process_timed_updates(timed_update_list &&timed_updates)
    {
        timer tp { fmt::format("validator epoch: {} process {} timed updates", _epoch, timed_updates.size()) };
        std::vector<tx_out_ref> collected_collateral {};
        uint64_t collateral_refund = 0;
        ::turbo::cardano::ledger::rules::foreach_transaction(timed_updates,
            [](const auto &upd) { return upd.loc; }, [&](auto transaction) {
                for (auto &upd: transaction)
                    _process_timed_update(collected_collateral, collateral_refund, std::move(upd));
                finish_transaction();
            });
        if (timed_updates.empty())
            finish_transaction();
        return { std::move(collected_collateral), collateral_refund };
    }

    void state::process_updates(updates_t &&updates)
    {
        const auto last_slot = updates.blocks.empty()
            ? std::optional<uint64_t> {} : std::optional<uint64_t> { updates.blocks.back().slot };
        {
            turbo::timer t { fmt::format("validator epoch {} process block updates: {}", _epoch, updates.blocks.size()), logger::level::trace };
            _process_block_updates(std::move(updates.blocks));
        }
        tx_out_ref_list collected_collateral {};
        uint64_t collateral_refund = 0;
        {
            turbo::timer t { fmt::format("validator epoch {} process timed updates: {}", _epoch, updates.timed.size()), logger::level::trace };
            auto collateral_updates = _process_timed_updates(std::move(updates.timed));
            collected_collateral = std::move(collateral_updates.first);
            collateral_refund = collateral_updates.second;
        }
        if (last_slot)
            _tick(*last_slot);
        {
            turbo::timer t { fmt::format("validator epoch {} process utxo updates batches: {}", _epoch, updates.utxos.size()), logger::level::trace };
            _process_utxo_updates(std::move(updates.utxos));
        }
        {
            turbo::timer t { fmt::format("validator epoch {} process collateral uses: {}", _epoch, collected_collateral.size()), logger::level::trace };
            _process_collateral_use(std::move(collected_collateral));
        }
        if (collateral_refund) {
            turbo::timer t { fmt::format("validator epoch {} process collateral refund: {}", _epoch, collateral_refund), logger::level::trace };
            sub_fees(collateral_refund);
        }
        run_pulser_if_ready();
    }
