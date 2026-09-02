#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/shelley/block.hpp>

namespace turbo::cardano::allegra {
    struct native_script_t {
        enum class type_t: uint8_t {
            signature = 0,
            all = 1,
            any = 2,
            at_least = 3,
            invalid_before = 4,
            invalid_hereafter = 5
        };

        type_t type {};
        key_hash key {};
        int64_t required = 0;
        std::vector<native_script_t> scripts {};
        uint64_t slot = 0;

        static native_script_t from_cbor(cbor::zero2::value &);
        static native_script_t from_cbor(buffer);
        static void validate_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;
    };

    struct block_header_base: shelley::block_header_base {
        using shelley::block_header_base::block_header_base;
    };

    struct block_header: shelley::block_header {
        using shelley::block_header::block_header;
    };

    struct block_base: shelley::block_base {
        using shelley::block_base::block_base;
    };

    struct transaction_body_t: shelley::transaction_body_t {
        std::optional<uint64_t> validity_start {};

        static transaction_body_t from_cbor(cbor::zero2::value &);
    };

    struct transaction_witness_set_t: shelley::transaction_witness_set_t {
        static transaction_witness_set_t from_cbor(cbor::zero2::value &);
    };

    struct tx: shelley::tx_base {
        tx(const cardano::block_base &, uint64_t blk_off, cbor::zero2::value &, size_t idx=0, bool invalid=false);
        const tx_hash &hash() const override;
        const input_set &inputs() const override;
        const tx_output_list &outputs() const override;
        uint64_t fee() const override;
        std::optional<uint64_t> validity_end() const override;
        std::optional<uint64_t> validity_start() const override;
        const withdrawal_map &withdrawals() const override;
        const cert_list &certs() const override;
        const param_update_proposal_list &updates() const override;
        buffer raw() const override;
        void parse_witnesses(cbor::zero2::value &) override;
    private:
        transaction_body_t _body;
    };

    struct block_transactions_t: block_tx_list<tx> {
        using block_tx_list<tx>::block_tx_list;
        static block_transactions_t from_cbor(const block_base &, const uint8_t *block_begin, cbor::zero2::array_reader &);
    };

    struct block: block_base {
        block(uint64_t era, uint64_t offset, uint64_t hdr_offset, cbor::zero2::value &, const cardano::config &);
        uint32_t body_size() const override;
        const cardano::block_header_base &header() const override;
        const block_hash &body_hash() const override;
        const tx_list &txs() const override;
    private:
        block_header _hdr;
        block_transactions_t _txs;
        block_meta_map _meta;
        mutable std::optional<block_hash> _body_hash {};
        const buffer _raw;

        block(uint64_t era, uint64_t offset, uint64_t hdr_offset, cbor::zero2::array_reader &, cbor::zero2::value &, const cardano::config &);
    };
}
