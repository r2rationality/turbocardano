/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/types.hpp>

namespace turbo::cardano {
    void pool_metadata::to_cbor(era_encoder &enc) const
    {
        enc.array(2)
            .text(url)
            .bytes(hash);
    }

    void relay_addr::to_cbor(era_encoder &enc) const
    {
        enc.array(4);
        enc.uint(0);
        port.to_cbor(enc);
        ipv4.to_cbor(enc);
        ipv6.to_cbor(enc);
    }

    void relay_host::to_cbor(era_encoder &enc) const
    {
        enc.array(3);
        enc.uint(1);
        port.to_cbor(enc);
        enc.text(host);
    }

    void relay_dns::to_cbor(era_encoder &enc) const
    {
        enc.array(2);
        enc.uint(2);
        enc.text(name);
    }

    void relay_info::to_cbor(era_encoder &enc) const
    {
        std::visit([&](const auto &v) {
            v.to_cbor(enc);
        }, val);
    }

    void pool_params::to_cbor(era_encoder &enc, const pool_hash &pool_id) const
    {
        enc.array(9);
        enc.bytes(pool_id);
        enc.bytes(vrf_vkey);
        enc.uint(pledge);
        enc.uint(cost);
        margin.to_cbor(enc);
        enc.bytes(reward_id);
        owners.to_cbor(enc);
        relays.to_cbor(enc);
        metadata.to_cbor(enc);
    }
}
