#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/ledger/shelley.hpp>

namespace turbo::cardano::ledger::shelley {
    template<std::integral T>
    size_t state::_param_to_cbor(era_encoder &enc, const size_t idx, const std::optional<T> &val)
    {
        if (val) {
            enc.uint(idx);
            enc.uint(numeric_cast<uint64_t>(*val));
            return 1;
        }
        return 0;
    }
}
