/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Utxow.
// Included at class scope by lib/turbo/txwit/validator.cpp.

            wit_cnt witnesses_ok_stage1(const block_container &blk, const tx_base &tx) const
            {
                const bool first_slot_ok = !intersection || blk.offset() >= intersection->end_offset;
                const bool last_slot_ok = !to || blk.offset() < to->end_offset;
                if (first_slot_ok && last_slot_ok) {
                    try {
                        switch (typ) {
                            case witness_type::all: {
                            case witness_type::vkey:
                                signer_set valid_vkeys {};
                                auto cnts = tx.witnesses_ok_vkey(valid_vkeys);
                                cnts += tx.witnesses_ok_native(valid_vkeys);
                                return cnts;
                            }
                            case witness_type::script:
                            case witness_type::none:
                                return {};
                            [[unlikely]] default: throw error(fmt::format("unsupported witness type: {}", static_cast<int>(typ)));
                        }
                    } catch (const std::exception &ex) {
                        const auto msg = fmt::format("txwit: slot: {} tx: {} error: {}", blk->slot(), tx.hash(), ex.what());
                        logger::error("{}", msg);
                        error_handler(msg);
                        if (blk->era() == 7)
                            throw;
                    }
                }
                return {};
            }
