/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/babbage/block.hpp>

namespace turbo::cardano::babbage {
    block_header::body_t::body_t(cbor::zero2::value &v, const cardano::config &cfg):
        body_t { v.array(), v, cfg }
    {
    }

    block_header::body_t::body_t(cbor::zero2::array_reader &it, cbor::zero2::value &v, const cardano::config &cfg):
        block_number { numeric_cast<decltype(block_number)>(it.read().uint()) },
        slot { numeric_cast<decltype(slot)>(it.read().uint()) },
        prev_hash { prev_hash_from_cbor(it.read(), cfg) },
        issuer_vkey { it.read().bytes() },
        vrf_vkey { it.read().bytes() },
        nonce_vrf { vrf_cert::from_cbor(it.read()) },
        body_size { numeric_cast<decltype(body_size)>(it.read().uint()) },
        body_hash { it.read().bytes() },
        op_cert { operational_cert::from_cbor(it.read()) },
        node_ver { protocol_version::from_cbor(it.read()) },
        raw { v.data_raw() }
    {
    }


    block_header::block_header(const uint64_t era, cbor::zero2::value &hdr, const cardano::config &cfg):
        block_header { era, hdr.array(), hdr, cfg }
    {
    }

    block_header::block_header(const uint64_t era, cbor::zero2::array_reader &it, cbor::zero2::value &hdr, const cardano::config &cfg):
        block_header_base { era, cfg },
        _body { it.read(), cfg },
        _sig { it.read().bytes() },
        _raw { hdr.data_raw() }
    {
    }
}
