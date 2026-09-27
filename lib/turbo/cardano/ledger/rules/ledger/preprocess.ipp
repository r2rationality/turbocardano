/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Ledger.
// Included at class scope by lib/turbo/txwit/validator.cpp.

            void pre_aggregate_data(batch_info &part, const block_container &blk, const bool defer_signatures=false) const
            {
                auto &stats = part.stats;
                auto &max = part.max_stats;
                uint64_t fees = 0;
                uint64_t donations = 0;
                if (!max.max_block_body_size || *max.max_block_body_size < blk->body_size())
                    max.max_block_body_size = numeric_cast<uint32_t>(blk->body_size());
                if (!max.max_block_header_size || *max.max_block_header_size < blk->header().size())
                    max.max_block_header_size = numeric_cast<uint16_t>(blk->header().size());
                blk->foreach_update_proposal([&](const auto &prop) {
                    part.timed_updates.emplace_back(make_timed_update(cert_loc_t { blk->slot(), 0, 0 }, prop));
                });
                blk->foreach_update_vote([&](const auto &vote) {
                    part.timed_updates.emplace_back(make_timed_update(cert_loc_t { blk->slot(), 0, 0 }, vote));
                });
                // Reference-script size accounting was introduced in protocol version 9,
                // which starts with the Conway era. Keep only compact join metadata here;
                // copying complete Babbage-and-later TXOs made the ordered stage replay an
                // ever-growing UTXO overlay serially.
                const auto first_ref_info_idx = part.ref_script_txs.size();
                if (blk->era() >= 7) {
                    auto &ref_block = part.ref_script_blocks.emplace_back();
                    ref_block.slot = blk->slot();
                    ref_block.height = numeric_cast<uint32_t>(blk->height());
                    ref_block.first_tx_idx = numeric_cast<uint32_t>(part.ref_script_txs.size());
                    for (const auto &tx_ptr: blk->txs()) {
                        const auto &tx = *tx_ptr;
                        const ref_script_position_t pos {
                            ref_block.height,
                            numeric_cast<uint32_t>(tx.index())
                        };
                        const auto tx_info_idx = numeric_cast<uint32_t>(part.ref_script_txs.size());
                        auto &ref_tx = part.ref_script_txs.emplace_back();
                        ref_tx = {
                            .tx_id=tx.hash(),
                            .pos=pos
                        };

                        flat_set<tx_out_ref> inputs {};
                        tx.foreach_input([&](const tx_input &input) {
                            inputs.emplace(input.hash, input.idx);
                        });
                        auto referenced = inputs;
                        tx.foreach_referenced_input([&](const tx_input &input) {
                            const tx_out_ref id { input.hash, input.idx };
                            if (inputs.contains(id))
                                ref_tx.regular_ref_overlap = id;
                            referenced.emplace(id);
                        });
                        for (const auto &id: referenced) {
                            auto &ref_part = part.ref_script_parts[txo_map::partition_idx(id)];
                            ref_part.uses.push_back({ id, tx_info_idx });
                        }
                        tx.foreach_collateral([&](const tx_input &input) {
                            const tx_out_ref id { input.hash, input.idx };
                            if (!referenced.contains(id))
                                part.ref_script_parts[txo_map::partition_idx(id)].uses.push_back({ id, tx_info_idx, false });
                        });

                        const auto add_consumption = [&](const tx_out_ref &id) {
                            auto &ref_part = part.ref_script_parts[txo_map::partition_idx(id)];
                            ref_part.consumed.push_back({ id, pos });
                        };
                        const auto add_production = [&](const tx_out_ref &id, const tx_output &output) {
                            auto &ref_part = part.ref_script_parts[txo_map::partition_idx(id)];
                            ref_part.produced.push_back({
                                id,
                                pos,
                                output.script_ref
                                    ? numeric_cast<uint32_t>(output.script_ref->script().size())
                                    : 0
                            });
                        };
                        if (tx.invalid()) {
                            tx.foreach_collateral([&](const tx_input &input) {
                                add_consumption(tx_out_ref { input.hash, input.idx });
                            });
                            if (const auto *babbage_tx = dynamic_cast<const cardano::babbage::tx_base *>(&tx); babbage_tx) {
                                if (const auto &collateral_return = babbage_tx->collateral_return(); collateral_return) {
                                    add_production(
                                        tx_out_ref { tx.hash(), tx_out_idx { tx.outputs().size() } },
                                        *collateral_return);
                                }
                            }
                        } else {
                            for (const auto &id: inputs)
                                add_consumption(id);
                            size_t output_idx = 0;
                            tx.foreach_output([&](const tx_output &output) {
                                add_production(
                                    tx_out_ref { tx.hash(), tx_out_idx { output_idx++ } },
                                    output);
                            });
                        }
                    }
                    ref_block.num_txs = numeric_cast<uint32_t>(part.ref_script_txs.size() - ref_block.first_tx_idx);
                }
                const auto block_info = storage::block_info::from_block(blk);
                size_t block_tx_idx = 0;
                for (const auto *tx_ptr: blk->txs()) {
                    const auto &tx = *tx_ptr;
                    const auto ref_info_idx = first_ref_info_idx + block_tx_idx++;
                    if (tx.invalid() && blk->era() != 7)
                        continue;
                    const uint8_t tx_part_idx = tx.hash()[0];
                    const size_t tx_idx = part.txs[tx_part_idx].size();
                    tx_loc_t tx_loc { tx_part_idx, tx_idx };
                    auto tx_checks = tx_context_t::from_tx(tx_loc, tx);
                    if (blk->era() >= 7) {
                        tx_checks.ref_info_idx = numeric_cast<uint32_t>(ref_info_idx);
                        if (part.ref_script_txs.at(ref_info_idx).tx_id != tx_checks.tx_id) [[unlikely]]
                            throw error("reference join transaction order differs from preprocessing order");
                    }
                    if (const auto *conway_tx = dynamic_cast<const cardano::conway::tx *>(&tx)) {
                        const auto &conway_block = dynamic_cast<const cardano::conway::block &>(*blk);
                        const auto auxiliary = conway_block.auxiliary_bytes(tx.index());
                        tx_checks.tx_size = cardano::ledger::rules::conway_tx_size(tx, auxiliary);
                        tx_checks.conway.collect(*conway_tx, auxiliary);
                        tx_checks.collateral_return = conway_tx->collateral_return();
                        tx_checks.total_collateral = conway_tx->collateral_value();
                    }
                    const auto witness_count = tx.witnesses().size();
                    tx_checks.signers.reserve(witness_count);
                    tx_checks.native_scripts.reserve(witness_count);
                    tx_checks.byron_signers.reserve(witness_count);
                    if (tx.block().era() > 1) {
                        if (!tx.invalid())
                            fees += tx.fee();
                        tx_checks.balances.out_coin += tx.fee();
                    }
                    if (!max.max_tx_size || *max.max_tx_size < tx_checks.tx_size)
                        max.max_tx_size = tx_checks.tx_size;
                    const auto num_redeemers = tx.redeemers().size();
                    if (const auto start_slot = tx.validity_start(); start_slot) {
                        if (*start_slot > blk->slot()) [[unlikely]]
                            throw error(fmt::format("tx {} validity start interval: {} starts after the block's slot: {}",
                                tx.hash(), *start_slot, blk->slot()));
                    }
                    // Utxo.inInterval uses an inclusive upper bound, with or without a lower bound.
                    if (const auto end_slot = tx.validity_end(); end_slot && *end_slot < blk->slot()) [[unlikely]]
                        throw error(fmt::format("tx {} validity end interval: {} ends before the block's slot: {}",
                            tx.hash(), *end_slot, blk->slot()));
                    if (num_redeemers) {
                        tx_checks.plutus_ctx.emplace(stored_tx_context {
                            .tx_id=tx.hash(),
                            .num_redeemers=num_redeemers,
                            .body=uint8_vector { tx.raw() },
                            .wits=uint8_vector { tx.witness_raw() },
                            // Stage 1 has no ledger state. Stage 2 fills the enacted protocol version.
                            .block=block_info
                        });
                        ++stats.num_plutus_txs;
                    } else {
                        ++stats.num_simple_txs;
                    }
                    tx.foreach_referenced_input([&](const tx_input &txi) {
                        auto &txo = tx_checks.ref_inputs.emplace_back(txi);
                        if (const auto txo_it = part.utxos.find(txo.id); txo_it != part.utxos.end())
                            txo.data = txo_it->second;
                    });
                    if (defer_signatures)
                        part.signature_checks.emplace_back(&blk, &tx);
                    else
                        stats.wit_cnts += witnesses_ok_stage1(blk, tx);
                    size_t cert_idx = 0;
                    tx.foreach_cert([&](const auto &cert) {
                        if (std::holds_alternative<instant_reward_cert>(cert.val))
                            tx_checks.reqires_genesis_delegs_quorum = true;
                        const cert_loc_t loc { blk->slot(), tx.index(), cert_idx++ };
                        std::visit([&](const auto &c) {
                            part.timed_updates.emplace_back(make_timed_update(loc, c, tx_loc, true, !tx.invalid()));
                        }, cert.val);
                        if (tx_checks.era != 7)
                            if (const auto cred = cert.signing_cred())
                                tx_checks.required_signers.emplace(*cred);
                    });
                    if (tx_checks.era == 7) {
                        // collect() has already hashed the supplied scripts. Reference
                        // scripts are added only later, during input resolution.
                        for (const auto &[hash, type]: tx_checks.conway.scripts) {
                            tx_checks.script_witnesses.emplace(hash);
                            if (type == script_type::native)
                                tx_checks.native_scripts.emplace(hash);
                        }
                    } else {
                        tx.foreach_script([&](const auto &s) {
                            if (tx_checks.era >= 7)
                                tx_checks.script_witnesses.emplace(s.hash());
                            if (s.type() == script_type::native)
                                tx_checks.native_scripts.emplace(s.hash());
                        });
                    }
                    // CERTS checks the registered reward balance in the ordered phase.
                    // Authorization is required even when the withdrawn amount is zero.
                    tx.foreach_withdrawal([&](const tx_withdrawal &withdr) {
                        const auto stake_id = withdr.address.stake_id();
                        if (!tx.invalid())
                            part.timed_updates.emplace_back(make_timed_update(
                                cert_loc_t { blk->slot(), tx.index(), 0 },
                                index::timed_update::stake_withdraw { stake_id, withdr.amount }
                            ));
                        tx_checks.balances.in_coin += withdr.amount.coins;
                        if (tx_checks.era != 7)
                            tx_checks.required_signers.emplace(withdrawal_signer(stake_id));
                    });
                    tx.foreach_collateral([&](const tx_input &input) {
                        auto &txo = tx_checks.collateral_inputs.emplace_back(input);
                        if (const auto it = part.utxos.find(txo.id); it != part.utxos.end()) {
                            if (!it->second) [[unlikely]]
                                throw error("collateral refers to an already consumed output");
                            txo.data = it->second;
                        }
                    });
                    size_t txo_idx = 0;
                    tx.foreach_output([&](const tx_output &txo) {
                        tx_checks.balances.out_coin += txo.coin;
                        if (const auto addr = txo.addr(); !addr.is_byron()) {
                            if (addr.network() != blk->config().shelley_network_id) [[unlikely]]
                                throw error(fmt::format("the network id of a shelley address: {} does not match the config: {}", addr, blk->config().shelley_network_id));
                        }
                        for (const auto &[policy_id, p_assets]: txo.assets) {
                            for (const auto &[name, coin]: p_assets) {
                                if (coin)
                                    tx_checks.balances.out_assets[policy_id][name] += coin;
                            }
                        }
                        if (!tx.invalid())
                            _add_utxo(part.utxos, tx, txo, txo_idx);
                        ++txo_idx;
                    });
                    if (tx_checks.era != 7) {
                        tx.foreach_required_signer([&](const auto vkey) {
                            tx_checks.required_signers.emplace(vkey_signer_t { vkey });
                        });
                    }
                    size_t num_inputs = 0;
                    tx.foreach_input([&](const tx_input &txin) {
                        auto &txo = tx_checks.inputs.emplace_back(txin);
                        if (tx.invalid()) {
                            if (const auto it = part.utxos.find(txo.id); it != part.utxos.end())
                                txo.data = it->second;
                        } else {
                            const auto [it, created] = part.utxos.try_emplace(txo.id);
                            if (!created)
                                txo.data = it->second;
                            _del_utxo(part, it, created);
                        }
                        ++num_inputs;
                    });
                    if (!num_inputs) [[unlikely]]
                        throw error(fmt::format("tx {} does not have any inputs!", tx.hash()));
                    const auto donation = tx.donation();
                    if (!tx.invalid())
                        donations += donation;
                    tx_checks.balances.out_coin += donation;
                    if (const auto *c_tx = dynamic_cast<const cardano::conway::tx *>(&tx); c_tx) {
                        index::timed_update::conway_tx_prelude prelude {
                            .has_proposals=!c_tx->proposals().empty()
                        };
                        for (const auto &v: c_tx->votes()) {
                            if (v.voter.type == voter_t::type_t::drep_key
                                    || v.voter.type == voter_t::type_t::drep_script) {
                                prelude.voting_dreps.emplace(
                                    v.voter.hash,
                                    v.voter.type == voter_t::type_t::drep_script);
                            }
                        }
                        tx.foreach_withdrawal([&](const tx_withdrawal &withdr) {
                            prelude.withdrawals.push_back({ reward_id_t { withdr.address.bytes() }, withdr.amount });
                        });
                        if (!tx.invalid() && (prelude.has_proposals || !prelude.voting_dreps.empty() || !prelude.withdrawals.empty())) {
                            part.timed_updates.emplace_back(make_timed_update(
                                cert_loc_t { blk->slot(), tx.index(), 0 },
                                std::move(prelude)
                            ));
                        }
                        size_t prop_idx = 0;
                        for (const auto &p: c_tx->proposals()) {
                            if (tx_checks.era != 7)
                                add_proposal_signer(tx_checks.required_signers, p.procedure.action);
                            if (!tx.invalid())
                                part.timed_updates.emplace_back(make_timed_update(
                                    cert_loc_t { blk->slot(), tx.index(), prop_idx }, p));
                            else
                                part.timed_updates.emplace_back(make_timed_update(
                                    cert_loc_t { blk->slot(), tx.index(), prop_idx }, p, tx_loc, true, false));
                            ++prop_idx;
                            if (!tx.invalid())
                                tx_checks.balances.out_coin += p.procedure.deposit;
                        }
                        size_t vote_idx = 0;
                        for (const auto &v: c_tx->votes()) {
                            if (tx_checks.era != 7)
                                tx_checks.required_signers.emplace(voter_signer(v.voter));
                            if (!tx.invalid())
                                part.timed_updates.emplace_back(make_timed_update(
                                    cert_loc_t { blk->slot(), tx.index(), vote_idx }, v));
                            ++vote_idx;
                        }
                    }
                    tx.foreach_witness_byron_vkey([&](const auto &w) {
                        tx_checks.byron_signers.emplace_back(w);
                    });
                    tx.foreach_witness_shelley_vkey([&](const auto &w) {
                        tx_checks.signers.emplace(vkey_signer_t { crypto::blake2b::digest<key_hash>(w.vkey) });
                    });
                    tx.foreach_witness_shelley_bootstrap([&](const auto &w) {
                        crypto::ed25519::vkey_full vk_full {};
                        if (sizeof(vk_full) != sizeof(w.vkey) + w.chain_code.size()) [[unlikely]]
                            throw error(fmt::format("invalid chain code size: {} (expected: {})", w.chain_code.size(), sizeof(vk_full) - sizeof(w.vkey)));
                        memcpy(vk_full.data(), w.vkey.data(), w.vkey.size());
                        memcpy(vk_full.data() + w.vkey.size(), w.chain_code.data(), w.chain_code.size());
                        tx_checks.signers.emplace(bootstrap_signer_t { byron_addr_root_hash(0, vk_full, w.attrs) });
                    });
                    tx.foreach_mint([&](const auto &policy_id, const auto &assets) {
                        bool minted = false;
                        for (const auto &[name, diff]: assets) {
                            // negative mint values signify an outflow of tokens
                            if (diff < 0) {
                                tx_checks.balances.out_assets[policy_id][name] += uint64_t { 0 } - static_cast<uint64_t>(diff);
                                minted = true;
                            } else if (diff > 0) {
                                tx_checks.balances.in_assets[policy_id][name] += diff;
                                minted = true;
                            }
                        }
                        if (minted && tx_checks.era != 7)
                            tx_checks.required_signers.emplace(script_signer_t { policy_id, redeemer_tag::mint });
                    });
                    if (tx.invalid()) {
                        ++stats.num_invalid_txs;
                        for (const auto &input: tx_checks.collateral_inputs)
                            _del_utxo(part, input.id);
                        if (tx_checks.collateral_return)
                            _add_utxo(part.utxos, tx, *tx_checks.collateral_return, tx.outputs().size());
                    }
                    part.txs[tx_part_idx].emplace_back(std::move(tx_checks));
                }
                blk->foreach_invalid_tx([&](const auto &tx) {
                    if (blk->era() == 7)
                        return; // already validated and reflected in the transaction-order overlay
                    ++stats.num_invalid_txs;
                    tx.foreach_collateral([&](const auto &txi) {
                        part.timed_updates.emplace_back(make_timed_update(
                            cert_loc_t { blk->slot(), tx.index(), 0 },
                            index::timed_update::collected_collateral_input { txi.hash, txi.idx }
                        ));
                    });
                    if (const auto *babbage_tx = dynamic_cast<const cardano::babbage::tx_base *>(&tx); babbage_tx) {
                        if (const auto c_ret = babbage_tx->collateral_return(); c_ret) {
                            part.timed_updates.emplace_back(make_timed_update(
                                cert_loc_t { blk->slot(), tx.index(), 0 },
                                index::timed_update::collected_collateral_refund { c_ret->coin }
                            ));
                            _add_utxo(part.utxos, tx, *c_ret, tx.outputs().size());
                        }
                    }
                });
                part.block_updates.emplace_back(blk->slot(), blk->issuer_hash(), fees, donations,
                    blk.end_offset(), numeric_cast<uint8_t>(blk->era()));
            }
