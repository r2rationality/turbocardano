/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include "messages.hpp"
#include <turbo/cbor/zero2.hpp>

namespace turbo::cardano::network::miniprotocol::chainsync {
    optional_point2 optional_point2::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        if (!it.done()) {
            return point2 { it.read().uint(), it.read().bytes() };
        }
        return {};
    }

    msg_request_next_t msg_request_next_t::from_cbor(cbor::zero2::array_reader &)
    {
        return {};
    }

    msg_await_reply_t msg_await_reply_t::from_cbor(cbor::zero2::array_reader &)
    {
        return {};
    }

    msg_roll_forward_t msg_roll_forward_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return { decltype(header)::from_cbor(it.read()), decltype(tip)::from_cbor(it.read()) };
    }

    msg_roll_backward_t msg_roll_backward_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return { decltype(target)::from_cbor(it.read()), decltype(tip)::from_cbor(it.read()) };
    }

    msg_find_intersect_t msg_find_intersect_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return { decltype(points)::from_cbor(it.read()) };
    }

    msg_intersect_found_t msg_intersect_found_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return { decltype(isect)::from_cbor(it.read()), decltype(tip)::from_cbor(it.read()) };
    }

    msg_intersect_not_found_t msg_intersect_not_found_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return { decltype(tip)::from_cbor(it.read()) };
    }

    msg_done_t msg_done_t::from_cbor(cbor::zero2::array_reader &)
    {
        return {};
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

}
