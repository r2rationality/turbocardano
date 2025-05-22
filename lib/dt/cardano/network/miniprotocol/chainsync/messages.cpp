/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/cbor/zero2.hpp>
#include "messages.hpp"

namespace daedalus_turbo::cardano::network::miniprotocol::chainsync {
    optional_point2 optional_point2::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        if (!it.done()) {
            return point2 { it.read().uint(), it.read().bytes() };
        }
        return {};
    }

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

    msg_request_next_t msg_request_next_t::from_cbor(cbor::zero2::array_reader &)
    {
        return {};
    }

    void msg_request_next_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(1);
        enc.uint(0);
    }

    msg_await_reply_t msg_await_reply_t::from_cbor(cbor::zero2::array_reader &)
    {
        return {};
    }

    void msg_await_reply_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(1);
        enc.uint(1);
    }

    msg_roll_forward_t msg_roll_forward_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return { decltype(header)::from_cbor(it.read()), decltype(tip)::from_cbor(it.read()) };
    }

    void msg_roll_forward_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(3);
        enc.uint(2);
        header.to_cbor(enc);
        tip.to_cbor(enc);
    }

    msg_roll_backward_t msg_roll_backward_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return { decltype(target)::from_cbor(it.read()), decltype(tip)::from_cbor(it.read()) };
    }

    void msg_roll_backward_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(3);
        enc.uint(3);
        target.to_cbor(enc);
        tip.to_cbor(enc);
    }

    msg_find_intersect_t msg_find_intersect_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return { decltype(points)::from_cbor(it.read()) };
    }

    void msg_find_intersect_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(2);
        enc.uint(4);
        points.to_cbor(enc);
    }

    msg_intersect_found_t msg_intersect_found_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return { decltype(isect)::from_cbor(it.read()), decltype(tip)::from_cbor(it.read()) };
    }

    void msg_intersect_found_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(3);
        enc.uint(5);
        isect.to_cbor(enc);
        tip.to_cbor(enc);
    }

    msg_intersect_not_found_t msg_intersect_not_found_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return { decltype(tip)::from_cbor(it.read()) };
    }

    void msg_intersect_not_found_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(2);
        enc.uint(6);
        tip.to_cbor(enc);
    }

    msg_done_t msg_done_t::from_cbor(cbor::zero2::array_reader &)
    {
        return {};
    }

    void msg_done_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(1);
        enc.uint(7);
    }

    msg_t msg_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        switch (const auto typ = it.read().uint(); typ) {
            case 0: return { msg_request_next_t::from_cbor(it) };
            case 1: return { msg_await_reply_t::from_cbor(it) };
            case 2: return { msg_roll_forward_t::from_cbor(it) };
            case 3: return { msg_roll_backward_t::from_cbor(it) };
            case 4: return { msg_find_intersect_t::from_cbor(it) };
            case 5: return { msg_intersect_found_t::from_cbor(it) };
            case 6: return { msg_intersect_not_found_t::from_cbor(it) };
            case 7: return { msg_done_t::from_cbor(it) };
            [[unlikely]] default: throw error(fmt::format("an unsupported type for a chainsync::msg_t: {}", typ));
        }
    }

    void msg_t::to_cbor(cbor::encoder &enc) const
    {
        std::visit([&](const auto &mv) {
            mv.to_cbor(enc);
        }, *this);
    }
}