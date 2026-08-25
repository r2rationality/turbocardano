/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/types.hpp>
#include <turbo/cbor/zero2.hpp>

namespace turbo::cardano {
    using namespace crypto;

    protocol_version protocol_version::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return { it.read().uint(), it.read().uint() };
    }

    vrf_cert vrf_cert::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return {
            it.read().bytes(),
            it.read().bytes(),
        };
    }

    operational_cert operational_cert::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return {
            it.read().bytes(),
            it.read().uint(),
            it.read().uint(),
            it.read().bytes(),
        };
    }
}
