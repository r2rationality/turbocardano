#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/cardano.hpp>
#include "types.hpp"

namespace daedalus_turbo::cardano::network::miniprotocol::chainsync
{
    struct msg_request_next_t {
        static msg_request_next_t from_cbor(cbor::zero2::array_reader &);
        void to_cbor(cbor::encoder &) const;
    };

    struct msg_await_reply_t {
        static msg_await_reply_t from_cbor(cbor::zero2::array_reader &);
        void to_cbor(cbor::encoder &) const;
    };

    struct msg_roll_forward_t {
        parsed_header header;
        point3 tip;

        static msg_roll_forward_t from_cbor(cbor::zero2::array_reader &);
        void to_cbor(cbor::encoder &) const;
    };
    static_assert(std::is_move_constructible_v<msg_roll_forward_t>);

    struct optional_point2: std::optional<point2> {
        using base_type = std::optional<point2>;
        using base_type::base_type;

        static optional_point2 from_cbor(cbor::zero2::value &);
        void to_cbor(cbor::encoder &) const;
    };

    struct msg_roll_backward_t {
        optional_point2 target;
        point3 tip;

        static msg_roll_backward_t from_cbor(cbor::zero2::array_reader &);
        void to_cbor(cbor::encoder &) const;
    };

    struct msg_find_intersect_t {
        point2_list points {};

        static msg_find_intersect_t from_cbor(cbor::zero2::array_reader &);
        void to_cbor(cbor::encoder &) const;
    };

    struct msg_intersect_found_t {
        point2 isect;
        point3 tip;

        static msg_intersect_found_t from_cbor(cbor::zero2::array_reader &);
        void to_cbor(cbor::encoder &) const;
    };

    struct msg_intersect_not_found_t {
        point3 tip;

        static msg_intersect_not_found_t from_cbor(cbor::zero2::array_reader &);
        void to_cbor(cbor::encoder &) const;
    };

    struct msg_done_t {
        static msg_done_t from_cbor(cbor::zero2::array_reader &);
        void to_cbor(cbor::encoder &) const;
    };

    struct msg_t: std::variant<msg_request_next_t, msg_await_reply_t, msg_roll_forward_t,
                    msg_roll_backward_t, msg_find_intersect_t, msg_intersect_found_t, msg_intersect_not_found_t,
                    msg_done_t> {
        using base_type = std::variant<msg_request_next_t, msg_await_reply_t, msg_roll_forward_t,
                    msg_roll_backward_t, msg_find_intersect_t, msg_intersect_found_t, msg_intersect_not_found_t,
                    msg_done_t>;
        using base_type::base_type;

        static msg_t from_cbor(cbor::zero2::value &);
        void to_cbor(cbor::encoder &) const;
    };
    static_assert(std::is_move_constructible_v<msg_t>);
    static_assert(std::is_copy_constructible_v<msg_t>);
}
