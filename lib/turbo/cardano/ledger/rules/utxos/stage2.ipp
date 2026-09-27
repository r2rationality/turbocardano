/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Utxo.
// Included at class scope by lib/turbo/txwit/validator.cpp.

            wit_cnt witnesses_ok_stage2(const block_base &blk, const tx_base &tx, const context &ctx, const bool expected_valid=true) const
            {
                const bool first_slot_ok = !intersection || blk.offset() >= intersection->end_offset;
                const bool last_slot_ok = !to || blk.offset() < to->end_offset;
                wit_cnt cnts {};
                if (first_slot_ok && last_slot_ok) {
                    try {
                        switch (typ) {
                            case witness_type::all:
                            case witness_type::script:
                                if (blk.era() == 7) {
                                    cardano::ledger::rules::validate_phase2_result(ctx.redeemers(), expected_valid,
                                        [&](const auto &redeemer) { return ctx.prepare_script(redeemer); },
                                        [&](auto &prepared) {
                                            ctx.eval_script(prepared);
                                            cnts += prepared.typ;
                                        });
                                } else {
                                    cnts += tx.witnesses_ok_plutus(ctx);
                                }
                                break;
                            default:
                                break;
                        }
                    } catch (const std::exception &ex) {
                        const auto msg = fmt::format("txwit slot: {} tx: {} error: {}", blk.slot(), tx.hash(), ex.what());
                        logger::error("{}", msg);
                        error_handler(msg);
                        if (blk.era() == 7)
                            throw;
                    }
                }
                return cnts;
            }
