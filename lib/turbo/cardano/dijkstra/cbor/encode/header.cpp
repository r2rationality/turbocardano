/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/block.hpp>

namespace turbo::cardano::dijkstra {
    void eb_announcement_t::to_cbor(era_encoder &enc) const
    {
        enc.array(2).bytes(hash).uint(size);
    }

    void block_header::to_cbor(era_encoder &enc) const
    {
        enc.array(2).array(12).uint(_body.block_number).uint(_body.slot);
        if (_body.prev_hash_is_null)
            enc.s_null();
        else
            enc.bytes(_body.prev_hash);
        enc.bytes(_body.issuer_vkey).bytes(_body.vrf_vkey);
        enc.array(2).bytes(_body.nonce_vrf.result).bytes(_body.nonce_vrf.proof);
        enc.uint(_body.body_size).bytes(_body.body_hash);
        enc.array(4)
            .bytes(_body.op_cert.hot_key)
            .uint(_body.op_cert.seq_no)
            .uint(_body.op_cert.period)
            .bytes(_body.op_cert.sig);
        _body.node_ver.to_cbor(enc);
        enc.boolean(_body.contains_leios_certificate);
        if (_body.eb_announcement)
            _body.eb_announcement->to_cbor(enc);
        else
            enc.s_null();
        enc.bytes(_signature);
    }
}
