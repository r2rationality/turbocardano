/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/types.hpp>

namespace turbo::cardano {
    void plutus_cost_model::to_cbor(era_encoder &enc) const
    {
        enc.array_compact(raw_values().size(), [&] {
            for (const auto cost: raw_values()) {
                if (cost >= 0)
                    enc.uint(cost);
                else
                    enc.nint(-(cost + 1));
            }
        });
    }

    void plutus_cost_models::to_cbor(era_encoder &enc) const
    {
        auto l_enc { enc };
        for (const auto &[id, m]: items) {
            l_enc.uint(id);
            m.to_cbor(l_enc);
        }
        if (items.empty()) [[unlikely]]
            throw error("a plutus_cost_model structure must have at least one model defined!");
        enc.map_compact(items.size(), [&] {
            enc << l_enc;
        });
    }
}

