/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/alonzo/block.hpp>

namespace turbo::cardano {
    tx_wit_datum tx_wit_datum::from_cbor(cbor::zero2::value &v)
    {
        const auto raw = v.data_raw();
        return { crypto::blake2b::digest<datum_hash>(raw), raw };
    }

}

namespace turbo::cardano::alonzo {
    namespace {
        redeemer_tag redeemer_tag_from_cbor(cbor::zero2::value &v)
        {
            switch (const auto typ = v.uint(); typ) {
                case 0: return redeemer_tag::spend;
                case 1: return redeemer_tag::mint;
                case 2: return redeemer_tag::cert;
                case 3: return redeemer_tag::reward;
                default: throw error(fmt::format("unsupported redeemer tag: {}", typ));
            }
        }
    }

    redeemer_t redeemer_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return {{
            redeemer_tag_from_cbor(it.read()),
            numeric_cast<uint32_t>(it.read().uint()),
            it.read().data_raw(),
            ex_units::from_cbor(it.read())
        }};
    }
}
