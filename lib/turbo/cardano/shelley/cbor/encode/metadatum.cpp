/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/shelley/metadata.hpp>

namespace turbo::cardano::shelley {
    void metadatum_t::to_cbor(era_encoder &enc) const
    {
        std::visit([&](const auto &item) {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, int64_t>) {
                if (item >= 0)
                    enc.uint(numeric_cast<uint64_t>(item));
                else
                    enc.nint(numeric_cast<uint64_t>(-(item + 1)));
            } else if constexpr (std::is_same_v<T, nint64_t>) {
                enc.uint(item);
            } else if constexpr (std::is_same_v<T, uint8_vector>) {
                enc.bytes(item);
            } else if constexpr (std::is_same_v<T, std::string>) {
                enc.text(item);
            } else if constexpr (std::is_same_v<T, array_t>) {
                enc.array_compact(item.size(), [&] {
                    for (const auto &value: item)
                        value.to_cbor(enc);
                });
            } else if constexpr (std::is_same_v<T, map_t>) {
                enc.map_compact(item.size(), [&] {
                    for (const auto &[key, value]: item) {
                        key.to_cbor(enc);
                        value.to_cbor(enc);
                    }
                });
            }
        }, value);
    }
}
