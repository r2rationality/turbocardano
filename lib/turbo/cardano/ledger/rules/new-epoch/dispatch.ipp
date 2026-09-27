/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Epoch.
// Included at namespace scope by lib/turbo/cardano/ledger/state.cpp.

    void state::start_epoch(const std::optional<uint64_t> new_epoch)
    {
        const auto prev_pv = _state->_params.protocol_ver;
        _state->start_epoch(new_epoch);
        if (!_vrf_state->kes_counters().empty())
            _vrf_state->finish_epoch(_state->_params.extra_entropy);
        const auto new_pv = _state->_params.protocol_ver;
        if (new_pv != prev_pv) {
            if (new_pv < prev_pv) [[unlikely]]
                throw error(fmt::format("protocol downgrades are not supported: went from {} to {}", prev_pv, new_pv));
            _transition_era(prev_pv.era(), new_pv.era());
            const auto tip_slot = slot::from_epoch(_state->_epoch, _state->_epoch_slot, _cfg);
            track_era(new_pv.era(), tip_slot);
        }
    }
