#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/types.hpp>

namespace turbo::cardano::mary::detail {
    enum class invalid_mint {
        zero_amount,
        empty_policy
    };

    using invalid_mint_observer = void (*)(invalid_mint);

    extern void decode_mint(cbor::zero2::value &, multi_mint_map &, invalid_mint_observer=nullptr);
}
