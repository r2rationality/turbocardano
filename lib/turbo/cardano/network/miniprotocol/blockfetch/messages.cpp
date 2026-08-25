/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include "messages.hpp"
#include <turbo/cbor/zero2.hpp>
#include <turbo/common/zstd.hpp>

namespace turbo::cardano::network::miniprotocol::blockfetch {
    msg_request_range_t msg_request_range_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return {
            decltype(from)::from_cbor(it.read()),
            decltype(to)::from_cbor(it.read())
        };
    }

    void msg_request_range_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(3);
        enc.uint(0);
        from.to_cbor(enc);
        to.to_cbor(enc);
    }

    void msg_client_done_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(1);
        enc.uint(1);
    }

    void msg_start_batch_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(1);
        enc.uint(2);
    }

    void msg_no_blocks_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(1);
        enc.uint(3);
    }

    msg_block_t msg_block_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return {
            { it.read().tag().read().bytes() }
        };
    }

    void msg_block_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(2);
        enc.uint(4);
        enc.tag(24);
        enc.bytes(bytes);
    }

    msg_compressed_blocks_t msg_compressed_blocks_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return {
            it.read().uint(), it.read().bytes()
        };
    }

    void msg_compressed_blocks_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(3);
        enc.uint(6);
        enc.uint(encoding);
        enc.bytes(payload);
    }

    uint8_vector msg_compressed_blocks_t::bytes() const
    {
        switch (encoding) {
            case encoding_raw: return payload;
            case encoding_zstd_fast:
            case encoding_zstd_max:
                return zstd::decompress(payload);
            [[unlikely]] default: throw error(fmt::format("unsupported encoding {}", encoding));
        }
    }

    int32_t msg_compressed_blocks_t::compression_level() const
    {
        switch (encoding) {
            case encoding_raw: return 0;
            case encoding_zstd_fast: return fast_compression_level;
            case encoding_zstd_max: return zstd::default_compression_level;
            [[unlikely]] default: throw error(fmt::format("unsupported encoding {}", encoding));
        }
    }

    uint64_t msg_compressed_blocks_t::encoding_for_compression_level(const int32_t compression_level)
    {
        return compression_level >= max_encoding_min_compression_level
            ? encoding_zstd_max
            : encoding_zstd_fast;
    }

    void msg_batch_done_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(1);
        enc.uint(5);
    }

    msg_t msg_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        switch (const auto typ = it.read().uint(); typ) {
            case 0: return { msg_request_range_t::from_cbor(it) };
            case 1: return { msg_client_done_t {} };
            case 2: return { msg_start_batch_t {} };
            case 3: return { msg_no_blocks_t {} };
            case 4: return { msg_block_t::from_cbor(it) };
            case 5: return { msg_batch_done_t {} };
            case 6: return { msg_compressed_blocks_t::from_cbor(it) };
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
