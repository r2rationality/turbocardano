/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/types.hpp>

namespace turbo::cardano {
    point2 point2::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return { it.read().uint(), it.read().bytes() };
    }

    point3 point3::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return { point2::from_cbor(it.read()), it.read().uint() };
    }

    optional_point2 optional_point2::from_cbor(cbor::zero2::value &v)
    {
        if (v.indefinite() || (v.special_uint() != 0 && v.special_uint() != 2))
            throw error("a protocol point must be an array of length zero or two");
        auto &it = v.array();
        if (it.done()) return {};
        return point2 { it.read().uint(), it.read().bytes() };
    }

    optional_point3 optional_point3::from_cbor(cbor::zero2::value &v)
    {
        if (v.indefinite() || v.special_uint() != 2)
            throw error("a protocol tip must be an array of length two");
        auto &it = v.array();
        // The reference codec consumes but ignores the height at origin.
        return { optional_point2::from_cbor(it.read()), it.read().uint() };
    }

    point point::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        const auto slot = it.read().uint();
        return { it.read().bytes(), slot, it.read().uint() };
    }
}
