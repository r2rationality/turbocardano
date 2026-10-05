/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/byron/block.hpp>

namespace turbo::cardano::byron {
    void protocol_version_t::to_cbor(era_encoder &enc) const
    {
        enc.array(3).uint(major).uint(minor).uint(alt);
    }
}
