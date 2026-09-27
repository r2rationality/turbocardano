/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <algorithm>
#include <turbo/cardano/ledger/conway.hpp>
#include <turbo/cardano/ledger/conway/detail.hpp>
#include <turbo/cardano/ledger/rules/gov/conway.hpp>

namespace turbo::cardano::ledger::conway {
#include <turbo/cardano/ledger/rules/epoch/conway.ipp>

#include <turbo/cardano/ledger/rules/ratify/snapshot.ipp>

    const pulsing_data_t &state::pulser_data() const
    {
        return _pulsing_data;
    }

    // Applies the effects produced by RATIFY inside the Agda EPOCH transition.

    // EPOCH. This method is called for every Conway epoch except the first.

}
