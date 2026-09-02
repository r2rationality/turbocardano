#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/types.hpp>

namespace turbo::cardano::alonzo::full::detail {
    struct transaction_body_raw_fields_t {
        std::optional<hash_32> auxiliary_data_hash {};
        std::optional<hash_32> script_data_hash {};
        std::optional<uint8_t> network_id {};
    };

    transaction_body_raw_fields_t transaction_body_raw_fields_from_cbor(buffer);
}
