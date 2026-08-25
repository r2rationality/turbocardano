/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/allegra/block.hpp>

namespace turbo::cardano::allegra {
    block_transactions_t block_transactions_t::from_cbor(const block_base &blk, const uint8_t *block_begin, cbor::zero2::array_reader &block_it)
    {
        std::vector<tx> decoded {};
        buffer txs_cbor {};
        {
            auto &txs_raw = block_it.read();
            if (!txs_raw.indefinite()) [[likely]]
                decoded.reserve(txs_raw.special_uint());
            auto &tx_it = txs_raw.array();
            while (!tx_it.done()) {
                auto &tx_raw = tx_it.read();
                decoded.emplace_back(blk, tx_raw.data_begin() - block_begin, tx_raw, decoded.size());
            }
            // data_raw() finalizes the value. Call it only after decoding the array,
            // and before block_it.read() reuses this decoder-level value.
            txs_cbor = txs_raw.data_raw();
        }
        auto &wits_raw = block_it.read();
        auto &wit_it = wits_raw.array();
        for (size_t end_i = decoded.size(), i = 0; !wit_it.done(); ++i) {
            if (i >= end_i) [[unlikely]]
                throw error("transaction witness count exceeds transaction count!");
            decoded[i].parse_witnesses(wit_it.read());
        }
        const auto wits_cbor = wits_raw.data_raw();
        return { std::move(decoded), txs_cbor, wits_cbor };
    }

    block::block(const uint64_t era, const uint64_t offset, const uint64_t hdr_offset, cbor::zero2::value &blk, const cardano::config &cfg):
        block { era, offset, hdr_offset, blk.array(), blk, cfg }
    {
    }

    block::block(const uint64_t era, const uint64_t offset, const uint64_t hdr_offset, cbor::zero2::array_reader &it, cbor::zero2::value &blk, const cardano::config &cfg):
        block_base { offset, hdr_offset },
        _hdr { era, it.read(), cfg },
        _txs { block_transactions_t::from_cbor(*this, blk.data_begin(), it) },
        _meta { block_meta_map::from_cbor(it.read()) },
        _raw { blk.data_raw() }
    {
    }
}
