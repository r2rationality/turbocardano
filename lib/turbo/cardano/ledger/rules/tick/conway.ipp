/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.RewardUpdate.
// Included at namespace scope by lib/turbo/cardano/ledger/conway-state.cpp.

    void state::_tick(const uint64_t slot)
    {
        babbage::state::_tick(slot);
        if (!_pulsing_data.drep_state_updated && slot > _pulsing_snapshot_slot) {
            logger::debug("slot: {} creating drep pulser snapshots", cardano::slot { slot, _cfg });
            _pulsing_data.drep_state_updated = true;
        }
    }
