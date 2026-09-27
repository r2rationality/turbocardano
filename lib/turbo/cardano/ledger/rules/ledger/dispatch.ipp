/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Ledger.
// Included at namespace scope by lib/turbo/cardano/ledger/state.cpp.

    void state::finish_update_processing(update_effects_t &&effects, const bool run_pulser)
    {
        _state->finish_certificates();
        _state->_process_collateral_use(std::move(effects.collected_collateral));
        if (effects.collateral_refund)
            _state->sub_fees(effects.collateral_refund);
        if (run_pulser)
            _state->run_pulser_if_ready();
    }
