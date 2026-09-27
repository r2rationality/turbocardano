/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Utxow.
// Included at class scope by lib/turbo/txwit/validator.cpp.

            void _validate_shelley_tx_invariants(const batch_info &part, tx_context_t &tx, const context *plutus_ctx) const
            {
                if (const auto it = part.tx_deposits.find(tx.tx_loc); it != part.tx_deposits.end()) {
                    tx.balances.in_coin += it->second.in_coin;
                    tx.balances.out_coin += it->second.out_coin;
                }
                if (!tx.balances.match()) [[unlikely]]
                    throw error(fmt::format("tx {}: consumed != produced: {}", tx.tx_id, tx.balances));
                if (plutus_ctx && tx.era != 7) {
                    // UTXO checked the budgets once, while computing minimum fees.
                    for (const auto &[rid, rdata]: plutus_ctx->redeemers())
                        tx.signers.emplace(script_signer_t { plutus_ctx->redeemer_script(rid), rid.tag });
                }
                std::optional<signer_set> vkey_signers {};
                const auto native_available = [&](const script_hash &hash) {
                    if (tx.native_scripts.contains(hash))
                        return true;
                    if (const auto it = tx.native_script_refs.find(hash); it != tx.native_script_refs.end()) {
                        const auto collect = [&](signer_set &vkeys) {
                            vkeys.reserve(tx.signers.size());
                            for (const auto &signer: tx.signers) {
                                if (const auto *key = std::get_if<vkey_signer_t>(&signer.val))
                                    vkeys.emplace(key->hash);
                            }
                        };
                        if (tx.era == 7)
                            cardano::ledger::conway::rules::utxow::validate_reference_native(
                                it->second, vkey_signers, tx.interval, tx.tx_id, collect);
                        else
                            cardano::ledger::conway::rules::utxow::validate_reference_native(
                                it->second, vkey_signers, tx.slot, tx.tx_id, collect);
                        return true;
                    }
                    return false;
                };
                if (tx.era == 7) {
                    const auto &needed = tx.conway.credentials;
                    if (!exact_script_witnesses(needed.scripts, tx.reference_scripts, tx.script_witnesses)) [[unlikely]]
                        throw error("script witness domain differs from needed minus reference scripts");
                    for (const auto &hash: needed.keys)
                        if (!has_key_witness(tx.signers, hash)) [[unlikely]]
                            throw error(fmt::format("missing required key witness: {}", hash));
                    for (const auto &hash: needed.scripts) {
                        if (tx.conway.scripts.at(hash) == script_type::native) {
                            if (!native_available(hash)) [[unlikely]]
                                throw error(fmt::format("missing required native script: {}", hash));
                        } else if (tx.conway.redeemers.empty()) {
                            // A collateral credential has no rdptr. With redeemers,
                            // collateral validation already requires key addresses.
                            throw error("required Plutus script has no invocation");
                        }
                    }
                    // validate() checked the exact redeemer domain. UTXOS will
                    // compare evaluation results with isValid; scripts are not signers.
                } else if (tx.era >= 7) {
                    flat_set<script_hash> needed {};
                    for (const auto &signer: tx.required_signers)
                        if (const auto *script = std::get_if<script_signer_t>(&signer.val))
                            needed.emplace(script->hash);
                    if (!exact_script_witnesses(needed, tx.reference_scripts, tx.script_witnesses)) [[unlikely]]
                        throw error("script witness domain differs from needed minus reference scripts");
                }
                const auto *missing = missing_required_witness(tx.required_signers, tx.signers, native_available);
                if (missing) [[unlikely]]
                    throw error(fmt::format("epoch: {} tx {} missing a required_signer: {}", part.epoch, tx.tx_id, *missing));
                if (tx.reqires_genesis_delegs_quorum) [[unlikely]] {
                    size_t num_signers = 0;
                    for (const auto &vk_hash: _cr.config().byron_delegate_hashes) {
                        if (has_key_witness(tx.signers, vk_hash))
                            ++num_signers;
                    }
                    const auto quorum = _cr.config().shelley_update_quorum;
                    if (num_signers < quorum) [[unlikely]]
                        throw error(fmt::format("a quorum of {} genesis delegates is required but got only: {}", quorum, num_signers));
                    logger::debug("epoch: {} tx: {} requires a quorum of {} genesis delegates and got {}",
                        part.epoch, tx.tx_id, quorum, num_signers);
                }
            }
