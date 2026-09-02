/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/shelley/metadata.hpp>

namespace turbo::cardano::shelley {
    void metadata_t::to_cbor(era_encoder &enc) const
    {
        enc.map_compact(dict.size(), [&] {
            for (const auto &[label, value]: dict) {
                enc.uint(label);
                value.to_cbor(enc);
            }
        });
    }
}
