#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/shelley/cbor/decode/transaction-input.hpp>

namespace turbo::cardano {
    inline tx_out_ref tx_out_ref::from_cbor(cbor::zero2::value &v)
    {
        return shelley::detail::transaction_input_from_cbor(v);
    }
}
