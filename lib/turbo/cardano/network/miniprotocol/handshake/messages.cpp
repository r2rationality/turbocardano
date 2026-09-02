/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include "messages.hpp"
#include <turbo/cbor/zero2.hpp>

namespace turbo::cardano::network::miniprotocol::handshake {

    msg_propose_versions_t msg_propose_versions_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return { decltype(versions)::from_cbor(it.read()) };
    }

    msg_accept_version_t msg_accept_version_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return { it.read().uint(), decltype(config)::from_cbor(it.read()) };
    }

    msg_refuse_t::version_mismatch_t msg_refuse_t::version_mismatch_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return { decltype(versions)::from_cbor(it.read()) };
    }

    msg_refuse_t::decode_error_t msg_refuse_t::decode_error_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return { it.read().uint(), std::string { it.read().text() } };
    }

    msg_refuse_t::refused_t msg_refuse_t::refused_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return { it.read().uint(), std::string { it.read().text() } };
    }

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

    msg_query_reply_t msg_query_reply_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return { decltype(versions)::from_cbor(it.read()) };
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

}
