/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/types.hpp>
#include <turbo/cbor/zero2.hpp>

namespace turbo::cardano {
    drep_t drep_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        const auto dtyp = it.read().uint();
        auto res = [&]() -> drep_t {
            switch (dtyp) {
                case 0: return { credential_t { it.read().bytes(), false } };
                case 1: return { credential_t { it.read().bytes(), true } };
                case 2: return { abstain_t {} };
                case 3: return { no_confidence_t {} };
                default: throw error(fmt::format("unsupported drep type: {}", dtyp));
            }
        }();
        if (!it.done()) [[unlikely]]
            throw error("unexpected trailing drep elements");
        return res;
    }
}
