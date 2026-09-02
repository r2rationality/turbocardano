#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/types.hpp>

namespace turbo::cardano {
    template<typename T>
    void nil_optional_t<T>::to_cbor(era_encoder &enc) const
    {
        if (base_type::has_value()) {
            value_to_cbor(enc, base_type::operator*());
        } else {
            enc.s_null();
        }
    }

    template<typename T>
    void prefix_optional_t<T>::to_cbor(era_encoder &enc) const
    {
        if (base_type::has_value()) {
            enc.array(2);
            enc.uint(1);
            value_to_cbor(enc, base_type::operator*());
        } else {
            enc.array(1);
            enc.uint(0);
        }
    }

    template<typename T>
    void array_optional_t<T>::to_cbor(era_encoder &enc) const
    {
        if (base_type::has_value()) {
            enc.array(1);
            value_to_cbor(enc, base_type::operator*());
        } else {
            enc.array(0);
        }
    }

    template<typename M, typename ENC>
    void map_to_cbor(ENC &enc, const M &m)
    {
        enc.map_compact(m.size(), [&] {
            for (const auto &[k, v]: m) {
                value_to_cbor(enc, k);
                value_to_cbor(enc, v);
            }
        });
    }

    template<typename K, typename V, typename ENC>
    void map_t<K, V, ENC>::to_cbor(ENC &enc) const
    {
        map_to_cbor(enc, *this);
    }

    template<typename T, typename ENC>
    void vector_t<T, ENC>::to_cbor(ENC &enc) const
    {
        enc.array_compact(base_type::size(), [&] {
            for (const auto &v: *this)
                value_to_cbor(enc, v);
        });
    }
}
