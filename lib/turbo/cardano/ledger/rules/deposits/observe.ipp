/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Utxo.
// Included at class scope by lib/turbo/txwit/validator.cpp.

            // A single-key scratch value suffices for valid transactions: CERTS
            // applies each update before the next observation. Invalid transactions
            // need a sparse overlay because their certificates never reach CERTS.
            struct deposit_observation_view {
                const state &ledger;
                batch_info::invalid_deposit_view *overlay = nullptr;
                cardano::ledger::rules::amount_sum scratch {};
                bool pool_present = false;

                void add_stake(const stake_ident &id, uint64_t amount)
                {
                    if (overlay)
                        stake(id) += amount;
                }

                void add_drep(const credential_t &id, uint64_t amount)
                {
                    if (overlay)
                        drep(id) += amount;
                }

                cardano::ledger::rules::amount_sum &stake(const stake_ident &id)
                {
                    const auto initial = [&] { return ledger.has_stake(id) ? ledger.stake_deposit(id) : 0; };
                    if (overlay) {
                        auto [it, added] = overlay->stake.try_emplace(id);
                        if (added)
                            it->second = initial();
                        return it->second;
                    }
                    scratch = initial();
                    return scratch;
                }

                cardano::ledger::rules::amount_sum &drep(const credential_t &id)
                {
                    if (overlay) {
                        auto [it, added] = overlay->drep.try_emplace(id);
                        if (added)
                            it->second = ledger.drep_deposit(id);
                        return it->second;
                    }
                    scratch = ledger.drep_deposit(id);
                    return scratch;
                }

                bool &pool(const pool_hash &id)
                {
                    if (overlay) {
                        auto [it, added] = overlay->pools.try_emplace(id, false);
                        if (added)
                            it->second = ledger.has_pool(id);
                        return it->second;
                    }
                    pool_present = ledger.has_pool(id);
                    return pool_present;
                }
            };

            void _observe_deposit_effects(batch_info &part, const timed_update_info_t &upd) const
            {
                if (!upd.has_tx_loc)
                    return;
                if (upd.apply && _st.params().protocol_ver.major < 9) {
                    _observe_legacy_deposits(part, upd);
                    return;
                }
                auto &overlay = part.invalid_deposits;
                if (!upd.apply && (!overlay.tx || *overlay.tx != upd.tx_loc)) {
                    overlay = {};
                    overlay.tx = upd.tx_loc;
                }
                deposit_observation_view view { _st, upd.apply ? nullptr : &overlay };
                const auto change = std::visit([&](const auto &c) -> deposit_info_t {
                    using T = std::decay_t<decltype(c)>;
                    if constexpr (std::is_same_v<T, proposal_t>) {
                        // Valid proposal charges are already part of the preprocessed
                        // output balance and are checked against govActionDeposit by GOV.
                        return upd.apply ? deposit_info_t {} : deposit_info_t { {}, _st.params().gov_action_deposit };
                    } else {
                        return cardano::ledger::rules::certificate_deposits(c, _st.params(), view);
                    }
                }, upd.update.update);
                if (!change.empty())
                    part.tx_deposits[upd.tx_loc] += change;
            }

            void _observe_legacy_deposits(batch_info &part, const timed_update_info_t &upd) const
            {
                std::visit([&](const auto &c) {
                    using T = std::decay_t<decltype(c)>;
                    if constexpr (std::is_same_v<T, stake_reg_cert>) {
                        if (!_st.has_stake(c.stake_id))
                            part.tx_deposits[upd.tx_loc].out_coin += _st.params().key_deposit;
                    } else if constexpr (std::is_same_v<T, reg_cert>
                            || std::is_same_v<T, stake_reg_deleg_cert>
                            || std::is_same_v<T, vote_reg_deleg_cert>
                            || std::is_same_v<T, stake_vote_reg_deleg_cert>) {
                        if (!_st.has_stake(c.stake_id))
                            part.tx_deposits[upd.tx_loc].out_coin += c.deposit;
                    } else if constexpr (std::is_same_v<T, stake_dereg_cert>) {
                        part.tx_deposits[upd.tx_loc].in_coin += _st.params().key_deposit;
                    } else if constexpr (std::is_same_v<T, unreg_cert>) {
                        part.tx_deposits[upd.tx_loc].in_coin += c.deposit;
                    } else if constexpr (std::is_same_v<T, pool_reg_cert>) {
                        if (!_st.has_pool(c.pool_id))
                            part.tx_deposits[upd.tx_loc].out_coin += _st.params().pool_deposit;
                    } else if constexpr (std::is_same_v<T, reg_drep_cert>) {
                        if (!_st.has_drep(c.drep_id))
                            part.tx_deposits[upd.tx_loc].out_coin += c.deposit;
                    } else if constexpr (std::is_same_v<T, unreg_drep_cert>) {
                        part.tx_deposits[upd.tx_loc].in_coin += c.deposit;
                    }
                }, upd.update.update);
            }
