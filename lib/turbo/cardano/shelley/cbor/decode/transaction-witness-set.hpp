#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/shelley/block.hpp>

namespace turbo::cardano::shelley::detail {
    using native_script_list_decoder = void (*)(transaction_witness_set_t &, cbor::zero2::value &);

    transaction_witness_set_t transaction_witness_set_from_cbor(
        cbor::zero2::value &, native_script_list_decoder);
}
