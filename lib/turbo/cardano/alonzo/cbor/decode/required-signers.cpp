/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/alonzo/block.hpp>

namespace turbo::cardano::alonzo {
    required_signers_t required_signers_t::from_cbor(cbor::zero2::value &v)
    {
        required_signers_t s {};
        if (!v.indefinite()) [[likely]]
            s.reserve(v.special_uint());
        auto &it = v.array();
        while (!it.done())
            s.emplace_hint(s.end(), it.read().bytes());
        return s;
    }
}
