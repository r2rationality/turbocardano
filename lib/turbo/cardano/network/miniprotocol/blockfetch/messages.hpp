#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/common.hpp>
#include "types.hpp"

namespace turbo::cardano::network::miniprotocol::blockfetch
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
        // The compressed encodings represent normalized quality classes, not the source's exact Zstandard level.
        static constexpr uint64_t encoding_raw = 0;
        static constexpr uint64_t encoding_zstd_fast = 1;
        static constexpr uint64_t encoding_zstd_max = 2;
        static constexpr int32_t fast_compression_level = 3;
        static constexpr int32_t max_encoding_min_compression_level = 21;

        uint64_t encoding;
        uint8_vector payload;

        static msg_compressed_blocks_t from_cbor(cbor::zero2::array_reader &);
        void to_cbor(cbor::encoder &) const;
        uint8_vector bytes() const;
        [[nodiscard]] int32_t compression_level() const;
        [[nodiscard]] static uint64_t encoding_for_compression_level(int32_t);
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
