#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/cardano/common/common.hpp>
#include "types.hpp"

namespace daedalus_turbo::cardano::network::miniprotocol::blockfetch
{
    struct msg_request_range_t {
        point2 from;
        point2 to;

        static msg_request_range_t from_cbor(cbor::zero2::array_reader &);
        void to_cbor(cbor::encoder &) const;
    };

    struct msg_client_done_t {
        void to_cbor(cbor::encoder &) const;
    };

    struct msg_start_batch_t {
        void to_cbor(cbor::encoder &) const;
    };

    struct msg_no_blocks_t {
        void to_cbor(cbor::encoder &) const;
    };

    struct msg_block_t {
        uint8_vector bytes;

        static msg_block_t from_cbor(cbor::zero2::array_reader &);
        void to_cbor(cbor::encoder &) const;
    };

    struct msg_compressed_blocks_t {
        uint64_t encoding;
        uint8_vector payload;

        static msg_compressed_blocks_t from_cbor(cbor::zero2::array_reader &);
        void to_cbor(cbor::encoder &) const;
        uint8_vector bytes() const;
    };

    struct msg_batch_done_t {
        void to_cbor(cbor::encoder &) const;
    };

    using msg_base_t = std::variant<msg_request_range_t, msg_client_done_t, msg_start_batch_t,
        msg_no_blocks_t, msg_block_t, msg_batch_done_t, msg_compressed_blocks_t>;
    struct msg_t: msg_base_t {
        using base_type = msg_base_t;
        using base_type::base_type;

        static msg_t from_cbor(cbor::zero2::value &);
        void to_cbor(cbor::encoder &) const;
    };
}
