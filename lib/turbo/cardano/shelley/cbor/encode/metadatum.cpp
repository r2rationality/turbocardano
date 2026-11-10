/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/shelley/metadata.hpp>
#include <boost/container/small_vector.hpp>

namespace turbo::cardano::shelley {
    void metadatum_t::to_cbor(era_encoder &enc) const
    {
        boost::container::small_vector<const metadatum_t *, 16> pending{this};
        while (!pending.empty()) {
            const auto *current = pending.back();
            pending.pop_back();
            if (!current) {
                enc.s_break();
                continue;
            }
            std::visit([&](const auto &item) {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<T, nint64_t>) {
                    enc.nint(item.raw);
                } else if constexpr (std::is_same_v<T, uint64_t>) {
                    enc.uint(item);
                } else if constexpr (std::is_same_v<T, uint8_vector>) {
                    enc.bytes(item);
                } else if constexpr (std::is_same_v<T, std::string>) {
                    enc.text(item);
                } else if constexpr (std::is_same_v<T, array_t>) {
                    if (item.size() >= 24) {
                        enc.array();
                        pending.push_back(nullptr);
                    } else {
                        enc.array(item.size());
                    }
                    for (auto it = item.rbegin(); it != item.rend(); ++it)
                        pending.push_back(&*it);
                } else if constexpr (std::is_same_v<T, map_t>) {
                    if (item.size() >= 24) {
                        enc.map();
                        pending.push_back(nullptr);
                    } else {
                        enc.map(item.size());
                    }
                    for (auto it = item.rbegin(); it != item.rend(); ++it) {
                        pending.push_back(&it->second);
                        pending.push_back(&it->first);
                    }
                }
            }, current->value);
        }
    }
}
