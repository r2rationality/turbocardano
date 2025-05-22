/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/cbor/zero2.hpp>
#include "messages.hpp"

namespace daedalus_turbo::cardano::network::miniprotocol::handshake {

    msg_propose_versions_t msg_propose_versions_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return { decltype(versions)::from_cbor(it.read()) };
    }

    void msg_propose_versions_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(2);
        enc.uint(0);
        versions.to_cbor(enc);
    }

    msg_accept_version_t msg_accept_version_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return { it.read().uint(), decltype(config)::from_cbor(it.read()) };
    }

    void msg_accept_version_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(3);
        enc.uint(1);
        enc.uint(version);
        config.to_cbor(enc);
    }

    msg_refuse_t::version_mismatch_t msg_refuse_t::version_mismatch_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return { decltype(versions)::from_cbor(it.read()) };
    }

    void msg_refuse_t::version_mismatch_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(2);
        enc.uint(0);
        versions.to_cbor(enc);
    }

    msg_refuse_t::decode_error_t msg_refuse_t::decode_error_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return { it.read().uint(), std::string { it.read().text() } };
    }

    void msg_refuse_t::decode_error_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(3);
        enc.uint(1);
        enc.uint(version);
        enc.text(msg);
    }

    msg_refuse_t::refused_t msg_refuse_t::refused_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return { it.read().uint(), std::string { it.read().text() } };
    }

    void msg_refuse_t::refused_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(3);
        enc.uint(2);
        enc.uint(version);
        enc.text(msg);
    };

    msg_refuse_t msg_refuse_t::from_cbor(cbor::zero2::array_reader &it)
    {
        auto &it2 = it.read().array();
        switch (const auto typ = it2.read().uint(); typ) {
            case 0: return { version_mismatch_t::from_cbor(it2) };
            case 1: return { decode_error_t::from_cbor(it2) };
            case 2: return { refused_t::from_cbor(it2) };
            [[unlikely]] default: throw error(fmt::format("unsupported message refuse reason: {}", typ));
        }
    }

    void msg_refuse_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(2);
        enc.uint(2);
        std::visit([&](const auto &rv) {
            rv.to_cbor(enc);
        }, reason);
    }

    msg_query_reply_t msg_query_reply_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return { decltype(versions)::from_cbor(it.read()) };
    }

    void msg_query_reply_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(2);
        enc.uint(3);
        versions.to_cbor(enc);
    }

    msg_t msg_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        switch (const auto typ = it.read().uint(); typ) {
            case 0: return { msg_propose_versions_t::from_cbor(it) };
            case 1: return { msg_accept_version_t::from_cbor(it) };
            case 2: return { msg_refuse_t::from_cbor(it) };
            case 3: return { msg_query_reply_t::from_cbor(it) };
            [[unlikely]] default: throw error(fmt::format("an unsupported type for a handshake::msg_t: {}", typ));
        }
    }

    void msg_t::to_cbor(cbor::encoder &enc) const
    {
        std::visit([&](const auto &mv) {
            mv.to_cbor(enc);
        }, *this);
    }
}