/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/network/miniprotocol/handshake/messages.hpp>

namespace turbo::cardano::network::miniprotocol::handshake {
    void msg_propose_versions_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(2);
        enc.uint(0);
        versions.to_cbor(enc);
    }

    void msg_accept_version_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(3);
        enc.uint(1);
        enc.uint(version);
        config.to_cbor(enc);
    }

    void msg_refuse_t::version_mismatch_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(2);
        enc.uint(0);
        versions.to_cbor(enc);
    }

    void msg_refuse_t::decode_error_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(3);
        enc.uint(1);
        enc.uint(version);
        enc.text(msg);
    }

    void msg_refuse_t::refused_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(3);
        enc.uint(2);
        enc.uint(version);
        enc.text(msg);
    }

    void msg_refuse_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(2);
        enc.uint(2);
        std::visit([&](const auto &rv) {
            rv.to_cbor(enc);
        }, reason);
    }

    void msg_query_reply_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(2);
        enc.uint(3);
        versions.to_cbor(enc);
    }

    void msg_t::to_cbor(cbor::encoder &enc) const
    {
        std::visit([&](const auto &mv) {
            mv.to_cbor(enc);
        }, *this);
    }
}
