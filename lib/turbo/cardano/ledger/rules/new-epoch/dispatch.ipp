/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Epoch.
// Included at namespace scope by lib/turbo/cardano/ledger/state.cpp.

    void state::start_epoch(const std::optional<uint64_t> new_epoch)
    {
        if (_cfg.initial_protocol_version.major >= 2 && _state->_epoch == 0 && !_state->_end_offset
                && new_epoch.value_or(0) == 0 && _state->_params.protocol_ver == _cfg.initial_protocol_version)
            return;
        const auto prev_pv = _state->_params.protocol_ver;
        auto transition_from = prev_pv.era();
        if (transition_from == 7
                && new_epoch.value_or(_state->_end_offset ? _state->_epoch + 1 : 0) == _state->_epoch + 1) {
            const auto next_pv = dynamic_cast<conway::state &>(*_state).next_protocol_version();
            if (next_pv.era() == 8) {
                _transition_era(transition_from, next_pv.era());
                transition_from = next_pv.era();
            }
        }
        _state->start_epoch(new_epoch);
        if (!_vrf_state->kes_counters().empty())
            _vrf_state->finish_epoch(_state->_params.extra_entropy);
        const auto new_pv = _state->_params.protocol_ver;
        if (new_pv != prev_pv) {
            if (new_pv < prev_pv) [[unlikely]]
                throw error{"protocol downgrades are not supported: went from {} to {}", prev_pv, new_pv};
            _transition_era(transition_from, new_pv.era());
            const auto tip_slot = slot::from_epoch(_state->_epoch, _state->_epoch_slot, _cfg);
            track_era(new_pv.era(), tip_slot);
        }
    }
