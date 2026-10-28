/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/block.hpp>

namespace turbo::cardano::dijkstra {
    namespace {
        protocol_version protocol_version_from_cbor(cbor::zero2::value &v)
        {
            auto &it = v.array();
            protocol_version res { it.read().uint(), numeric_cast<uint32_t>(it.read().uint()) };
            if (res.major > 13) [[unlikely]]
                throw error{"unsupported Dijkstra protocol major version: {}", res.major};
            if (!it.done()) [[unlikely]]
                throw error("unexpected trailing Dijkstra protocol version elements");
            return res;
        }

        vrf_cert vrf_cert_from_cbor(cbor::zero2::value &v)
        {
            auto &it = v.array();
            vrf_cert result { it.read().bytes(), it.read().bytes() };
            if (result.result.size() != sizeof(vrf_result)) [[unlikely]]
                throw error("Dijkstra VRF output must contain exactly 64 bytes");
            if (!it.done()) [[unlikely]]
                throw error("unexpected trailing Dijkstra VRF certificate elements");
            return result;
        }

        operational_cert operational_cert_from_cbor(cbor::zero2::value &v)
        {
            auto &it = v.array();
            operational_cert result {
                it.read().bytes(), it.read().uint(), it.read().uint(), it.read().bytes()
            };
            if (!it.done()) [[unlikely]]
                throw error("unexpected trailing Dijkstra operational certificate elements");
            return result;
        }
    }

    eb_announcement_t eb_announcement_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        eb_announcement_t res {
            it.read().bytes(),
            numeric_cast<uint32_t>(it.read().uint())
        };
        if (!it.done()) [[unlikely]]
            throw error("unexpected trailing epoch-block announcement elements");
        return res;
    }

    block_header::block_header(const uint64_t era, cbor::zero2::value &v, const cardano::config &cfg):
        block_header_base { era, cfg }
    {
        auto &header_it = v.array();
        auto &body = header_it.read();
        auto &it = body.array();
        _block_number = it.read().uint();
        _slot = it.read().uint();
        auto &previous = it.read();
        if (!previous.is_null())
            _prev_hash.emplace(previous.bytes());
        else
            static_cast<void>(previous.special());
        _issuer_vkey = it.read().bytes();
        _vrf_vkey = it.read().bytes();
        _nonce_vrf = vrf_cert_from_cbor(it.read());
        _body_size = numeric_cast<uint32_t>(it.read().uint());
        _body_hash = it.read().bytes();
        _op_cert = operational_cert_from_cbor(it.read());
        _node_ver = protocol_version_from_cbor(it.read());
        _contains_leios_certificate = it.read().boolean();
        auto &announcement = it.read();
        if (!announcement.is_null())
            _eb_announcement.emplace(eb_announcement_t::from_cbor(announcement));
        else
            static_cast<void>(announcement.special());
        if (!it.done()) [[unlikely]]
            throw error("unexpected trailing Dijkstra header body elements");
        _body_raw = body.data_raw();
        _signature = header_it.read().bytes();
        if (!header_it.done()) [[unlikely]]
            throw error("unexpected trailing Dijkstra header elements");
        _raw = v.data_raw();
    }
}
