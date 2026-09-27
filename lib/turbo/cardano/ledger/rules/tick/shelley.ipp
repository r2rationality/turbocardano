/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.RewardUpdate.
// Included at namespace scope by lib/turbo/cardano/ledger/shelley.cpp.

    void state::_tick(const uint64_t slot)
    {
        _apply_future_shelley_delegs(slot);
        if (_params.protocol_ver.major >= 2)
            _ensure_reward_pulsing_snapshot(slot);
    }
