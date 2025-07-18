#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cardano/common/common.hpp>
#include <turbo/storage/chunk-info.hpp>

namespace turbo::cardano::mocks {
    /*
     * These are temporary short-lived data structures.
     * For that reason it is OK and valuable to store the reference to a storage::block_info
     */
    struct block_header: block_header_base {
        block_header(const uint64_t era, const storage::block_info &block_meta, const cardano::config &cfg=cardano::config::get()) :
            block_header_base { era, cfg },
            _block_meta { block_meta }
        {
        }

        const buffer &data_raw() const override
        {
            throw error("mocks::block_header::data_raw is not implemented!");
        }

        const block_hash &hash() const override
        {
            return _block_meta.hash;
        }

        uint64_t height() const override
        {
            return _block_meta.height;
        }

        uint64_t slot() const override
        {
            return _block_meta.slot;
        }

        const block_hash &prev_hash() const override
        {
            throw error("internal error: prev_hash() is unsupported for mock blocks!");
        }

        protocol_version protocol_ver() const override
        {
            throw error("internal error: protocol_ver() is unsupported for mock blocks!");
        }

        buffer issuer_vkey() const override
        {
            throw error("internal error: issuer_vkey() is unsupported for mock blocks!");
        }
    private:
        const storage::block_info &_block_meta;
    };

    struct block: block_base {
        // offset can be overridden by a transaction offset for mock block representing a single transaction
        block(const uint64_t offset, const storage::block_info &block_meta, const cardano::config &cfg):
            block_base { offset, block_meta.header_offset },
            _hdr { block_meta.era, block_meta, cfg }
        {
        }

        uint32_t body_size() const override
        {
            throw error("mocks::block::body_size is unsupported for mock blocks!");
        }

        const block_header_base &header() const override
        {
            return _hdr;
        }

        const tx_list &txs() const override
        {
            throw error("internal error: txs() is unsupported for mock blocks!");
        }
    private:
        block_header _hdr;
    };
}