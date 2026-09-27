/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: shared historical compatibility; see doc/conway-agda-rule-map.md.
// Included at namespace scope by lib/turbo/cardano/ledger/babbage.cpp.

    void state::_apply_param_update(const param_update &update)
    {
        std::string update_desc = _params.apply(update);
        logger::info("epoch: {} protocol params update: [ {}]", _epoch, update_desc);
    }
