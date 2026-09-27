/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Ledger.
// Included at class scope by lib/turbo/txwit/validator.cpp.

            void apply_batch(batch_info &&part)
            {
                _apply_epoch_update(part);
                _validate_signatures(part);
                auto update_effects = _apply_ledger_updates_before_witnesses(part);
                _cnts += _validate_witnesses_and_invariants(part);
                for (auto &frame: part.tx_frames)
                    frame.clear();
                _apply_ledger_updates_after_witnesses(part, std::move(update_effects));
                part.input_blocks.clear();
                part.input_bytes.clear();
            }
