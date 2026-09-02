/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/types.hpp>

namespace turbo::cardano {
    void drep_t::to_cbor(era_encoder &enc) const
    {
        std::visit([&](const auto &c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, credential_t>) {
                enc.array(2).uint(c.script ? 1 : 0);
                enc.bytes(c.hash);
            } else if constexpr (std::is_same_v<T, abstain_t>) {
                enc.array(1).uint(2);
            } else if constexpr (std::is_same_v<T, no_confidence_t>) {
                enc.array(1).uint(3);
            } else {
                throw error(fmt::format("unsupported drep type: {}", typeid(T).name()));
            }
        }, val);
    }
}

