#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/common.hpp>

namespace turbo::cardano::conway::detail {
    void transaction_witness_set_to_cbor(
        era_encoder &, const tx_wit_list &, const tx_redeemer_map &);
}
