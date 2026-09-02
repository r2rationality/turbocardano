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
                throw error(fmt::format("unsupported Dijkstra protocol major version: {}", res.major));
            if (!it.done()) [[unlikely]]
                throw error("unexpected trailing Dijkstra protocol version elements");
            return res;
        }

        vrf_cert vrf_cert_from_cbor(cbor::zero2::value &v)
        {
            auto &it = v.array();
            vrf_cert result { it.read().bytes(), it.read().bytes() };
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

    block_header::body_t::body_t(cbor::zero2::value &v, const cardano::config &cfg)
    {
        auto &it = v.array();
        block_number = it.read().uint();
        slot = it.read().uint();
        auto &previous = it.read();
        prev_hash_is_null = previous.is_null();
        prev_hash = prev_hash_from_cbor(previous, cfg);
        issuer_vkey = it.read().bytes();
        vrf_vkey = it.read().bytes();
        nonce_vrf = vrf_cert_from_cbor(it.read());
        body_size = numeric_cast<uint32_t>(it.read().uint());
        body_hash = it.read().bytes();
        op_cert = operational_cert_from_cbor(it.read());
        node_ver = protocol_version_from_cbor(it.read());
        contains_leios_certificate = it.read().boolean();
        auto &announcement = it.read();
        if (!announcement.is_null())
            eb_announcement.emplace(eb_announcement_t::from_cbor(announcement));
        else
            static_cast<void>(announcement.special());
        if (!it.done()) [[unlikely]]
            throw error("unexpected trailing Dijkstra header body elements");
        raw = v.data_raw();
    }

    block_header::block_header(const uint64_t era, cbor::zero2::value &v, const cardano::config &cfg):
        block_header { era, v.array(), v, cfg }
    {
    }

    block_header::block_header(const uint64_t era, cbor::zero2::array_reader &it,
            cbor::zero2::value &v, const cardano::config &cfg):
        block_header_base { era, cfg },
        _body { it.read(), cfg },
        _signature { it.read().bytes() }
    {
        if (!it.done()) [[unlikely]]
            throw error("unexpected trailing Dijkstra header elements");
        _raw = v.data_raw();
    }
}
