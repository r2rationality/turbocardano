#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/types/base.hpp>

namespace turbo::cardano {
    template<typename T, typename ENC>
    void value_to_cbor(ENC &enc, const T &v)
    {
        if constexpr (std::is_same_v<uint64_t, T>) {
            enc.uint(v);
        } else if constexpr (std::is_same_v<uint32_t, T>) {
            enc.uint(v);
        } else if constexpr (std::is_same_v<uint16_t, T>) {
            enc.uint(v);
        } else if constexpr (std::is_same_v<uint8_t, T>) {
            enc.uint(v);
        } else if constexpr (std::is_convertible_v<T, buffer>) {
            enc.bytes(v);
        } else if constexpr (std::is_same_v<float, T>) {
            enc.float32(v);
        } else {
            v.to_cbor(enc);
        }
    }

    template<typename T>
    void set_t<T>::to_cbor(era_encoder &enc) const
    {
        if (enc.era() == era_t::conway || enc.era() == era_t::dijkstra)
            enc.tag(258);
        enc.array_compact(base_type::size(), [&] {
            for (const auto &v: *this)
                value_to_cbor(enc, v);
        });
    }
}
