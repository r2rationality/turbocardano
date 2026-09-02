/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/shelley/block.hpp>

namespace turbo::cardano::shelley {
    buffer block_header_base::prev_hash_from_cbor(
        cbor::zero2::value &v, const cardano::config &cfg, bool *is_null)
    {
        const bool null = v.is_null();
        if (is_null)
            *is_null = null;
        return !null ? v.bytes() : buffer { cfg.byron_genesis_hash };
    }

}
