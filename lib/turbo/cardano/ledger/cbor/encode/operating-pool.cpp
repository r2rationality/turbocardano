/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/ledger/types.hpp>

namespace turbo::cardano::ledger {
    void operating_pool_info::to_cbor(era_encoder &enc) const
    {
        enc.array(3);
        switch (enc.era()) {
            case era_t::conway:
            case era_t::dijkstra:
                rel_stake.to_cbor(enc);
                break;
            default:
                enc.array(2)
                    .uint(rel_stake.numerator)
                    .uint(rel_stake.denominator);
                break;
        }
        enc.uint(active_stake);
        enc.bytes(vrf_vkey);
    }

    void operating_pool_map::to_cbor(era_encoder &enc) const
    {
        enc.array(2);
        map_to_cbor(enc, *this);
        enc.uint(total_stake);
    }
}

