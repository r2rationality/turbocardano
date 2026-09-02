/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/network/miniprotocol/chainsync/messages.hpp>

namespace turbo::cardano::network::miniprotocol::chainsync {
    void optional_point2::to_cbor(cbor::encoder &enc) const
    {
        if (has_value()) {
            enc.array(2);
            enc.uint(operator*().slot);
            enc.bytes(operator*().hash);
        } else {
            enc.array(0);
        }
    }

    void msg_request_next_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(1);
        enc.uint(0);
    }

    void msg_await_reply_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(1);
        enc.uint(1);
    }

    void msg_roll_forward_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(3);
        enc.uint(2);
        header.to_cbor(enc);
        tip.to_cbor(enc);
    }

    void msg_roll_backward_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(3);
        enc.uint(3);
        target.to_cbor(enc);
        tip.to_cbor(enc);
    }

    void msg_find_intersect_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(2);
        enc.uint(4);
        points.to_cbor(enc);
    }

    void msg_intersect_found_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(3);
        enc.uint(5);
        isect.to_cbor(enc);
        tip.to_cbor(enc);
    }

    void msg_intersect_not_found_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(2);
        enc.uint(6);
        tip.to_cbor(enc);
    }

    void msg_done_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(1);
        enc.uint(7);
    }

    void msg_t::to_cbor(cbor::encoder &enc) const
    {
        std::visit([&](const auto &mv) {
            mv.to_cbor(enc);
        }, *this);
    }
}

