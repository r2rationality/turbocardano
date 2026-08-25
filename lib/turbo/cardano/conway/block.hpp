#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/conway/transaction.hpp>

namespace turbo::cardano::conway {
    struct block_header_base: babbage::block_header_base {
    };

    struct block_header: babbage::block_header {
        using babbage::block_header::block_header;
    };

    struct block_base: babbage::block_base {
        using babbage::block_base::block_base;
    };

    struct block_transactions_t: block_tx_list<tx> {
        using block_tx_list<tx>::block_tx_list;
        static block_transactions_t from_cbor(const block_base &, const uint8_t *block_begin, cbor::zero2::array_reader &);
    };

    struct block: block_base {
        block(uint64_t era, uint64_t offset, uint64_t hdr_offset, cbor::zero2::value &block_tuple, const cardano::config &cfg);
        uint32_t body_size() const override;
        const cardano::block_header_base &header() const override;
        const block_hash &body_hash() const override;
        const tx_list &txs() const override;
        const invalid_tx_set &invalid_txs() const override;
    private:
        babbage::block_header _hdr;
        block_transactions_t _txs;
        auxiliary_data_dict_t _meta;
        invalid_tx_set _invalid_txs;
        mutable std::optional<block_hash> _body_hash {};
        const buffer _raw;

        block(uint64_t era, uint64_t offset, uint64_t hdr_offset, cbor::zero2::array_reader &it, cbor::zero2::value &block_tuple, const cardano::config &cfg);
    };

    extern void protocol_params_to_cbor(era_encoder &enc, const protocol_params &params);
}
