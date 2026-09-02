/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/network/miniprotocol/blockfetch/messages.hpp>

namespace turbo::cardano::network::miniprotocol::blockfetch {
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

    void msg_block_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(2);
        enc.uint(4);
        enc.tag(24);
        enc.bytes(bytes);
    }

    void msg_compressed_blocks_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(3);
        enc.uint(6);
        enc.uint(encoding);
        enc.bytes(payload);
    }

    void msg_batch_done_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(1);
        enc.uint(5);
    }

    void msg_t::to_cbor(cbor::encoder &enc) const
    {
        std::visit([&](const auto &mv) {
            mv.to_cbor(enc);
        }, *this);
    }
}

