/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Ledger.
// Included at class scope by lib/turbo/index/timed-update.hpp.

        void index_tx(const cardano::tx_base &tx) override
        {
            const auto slot = tx.block().slot();
            if (const auto *c_tx = dynamic_cast<const cardano::conway::tx *>(&tx); c_tx) {
                conway_tx_prelude prelude { .has_proposals=!c_tx->proposals().empty() };
                for (const auto &v: c_tx->votes()) {
                    if (v.voter.type == cardano::voter_t::type_t::drep_key
                            || v.voter.type == cardano::voter_t::type_t::drep_script) {
                        prelude.voting_dreps.emplace(
                            v.voter.hash,
                            v.voter.type == cardano::voter_t::type_t::drep_script);
                    }
                }
                tx.foreach_withdrawal([&](const auto &with) {
                    prelude.withdrawals.push_back({ cardano::reward_id_t { with.address.bytes() }, with.amount });
                });
                if (prelude.has_proposals || !prelude.voting_dreps.empty() || !prelude.withdrawals.empty())
                    _data.emplace_back(cardano::cert_loc_t { slot, tx.index(), 0 }, std::move(prelude));
                size_t cert_idx = 0;
                c_tx->foreach_cert([&](const auto &cert) {
                    std::visit([&](const auto &c) {
                        _data.emplace_back(cardano::cert_loc_t { slot, tx.index(), cert_idx++ }, c);
                    }, cert.val);
                });
                {
                    size_t prop_idx = 0;
                    for (const auto &p: c_tx->proposals())
                        _data.emplace_back(cardano::cert_loc_t { slot, tx.index(), prop_idx++ }, p);
                }
                {
                    size_t vote_idx = 0;
                    for (const auto &v: c_tx->votes())
                        _data.emplace_back(cardano::cert_loc_t { slot, tx.index(), vote_idx++ }, v);
                }
            } else {
                size_t cert_idx = 0;
                tx.foreach_cert([&](const auto &cert) {
                    std::visit([&](const auto &c) {
                        _data.emplace_back(cardano::cert_loc_t { slot, tx.index(), cert_idx++ }, c);
                    }, cert.val);
                });
            }
            tx.foreach_withdrawal([&](const auto &with) {
                _data.emplace_back(cardano::cert_loc_t { slot, tx.index(), 0 }, stake_withdraw { with.address.stake_id(), with.amount });
            });
        }

        void index_invalid_tx(const cardano::tx_base &tx) override
        {
            const auto slot = tx.block().slot();
            if (const auto *babbage_tx = dynamic_cast<const cardano::babbage::tx_base *>(&tx); babbage_tx) {
                if (const auto c_ret = babbage_tx->collateral_return(); c_ret)
                    _data.emplace_back(cardano::cert_loc_t { slot, tx.index(), 0 }, collected_collateral_refund { c_ret->coin });
            }
            tx.foreach_collateral([&](const auto &tx_in) {
                _data.emplace_back(cardano::cert_loc_t { slot, tx.index(), 0 }, collected_collateral_input { tx_in.hash, tx_in.idx });
            });
        }
