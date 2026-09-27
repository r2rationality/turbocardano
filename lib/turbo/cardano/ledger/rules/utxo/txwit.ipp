/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Utxo.
// Included at class scope by lib/turbo/txwit/validator.cpp.

            void _validate_tx_invariants(const batch_info &part, tx_context_t &tx, const context *plutus_ctx) const
            {
                try {
                    if (tx.era == 7) {
                        tx.conway.validate(_st.params(), &_language_views, _st.treasury());
                        if (!tx.phase2_valid && !plutus_ctx)
                            throw error("invalid flag on a transaction without phase-2 scripts");
                    }
                    ex_units declared_ex_units {};
                    if (plutus_ctx)
                        declared_ex_units = plutus_ctx->validate_redeemer_budgets(_st.params().max_tx_ex_units);
                    size_t ref_script_size = 0;
                    if (tx.era >= 7) {
                        if (part.ref_script_txs.at(tx.ref_info_idx).tx_id != tx.tx_id) [[unlikely]]
                            throw error("reference join result does not belong to the transaction");
                        ref_script_size = part.ref_script_sizes.at(tx.ref_info_idx);
                    }
                    const auto min_fee = _fees.minimum_fee(
                        tx.tx_size,
                        tx.era,
                        declared_ex_units,
                        ref_script_size);
                    if (cpp_int { tx.fee } < min_fee) [[unlikely]]
                        throw error(fmt::format("epoch: {} tx {} an insufficient fee for a tx of size {}: {} < {}",
                            part.epoch, tx.tx_id, static_cast<size_t>(tx.tx_size), tx.fee, min_fee));
                    switch (tx.era) {
                        case 0:
                            throw error(fmt::format("transaction {} in era 0!", tx.tx_id));
                        case 1:
                            _validate_byron_tx_invariants(part, tx);
                            break;
                        default:
                            _validate_shelley_tx_invariants(part, tx, plutus_ctx);
                            break;
                    }
                } catch (const std::exception &ex) {
                    const auto msg = fmt::format("txwit epoch: {} tx: {}: {}", part.epoch, tx.tx_id, ex.what());
                    logger::error("{}", msg);
                    _cfg.error_handler(msg);
                    if (tx.era == 7)
                        throw;
                }
            }
