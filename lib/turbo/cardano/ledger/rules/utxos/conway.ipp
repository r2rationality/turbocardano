/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Utxo.
// Included at namespace scope by lib/turbo/cardano/ledger/conway-state.cpp.

    void state::_process_block_updates(block_update_list &&block_updates)
    {
        for (const auto &bu: block_updates)
            _donations += bu.donations;
        babbage::state::_process_block_updates(std::move(block_updates));
    }
