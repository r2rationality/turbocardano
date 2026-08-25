#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/mary/block.hpp>

namespace turbo::cardano::alonzo {
    struct tx;

    struct block_header_base: mary::block_header_base {
        using mary::block_header_base::block_header_base;
    };

    struct block_header: mary::block_header {
        using mary::block_header::block_header;
    };

    struct block_base: mary::block_base {
        using mary::block_base::block_base;
        static block_hash compute_body_hash(const buffer &txs_raw, const buffer &wits_raw, const buffer &meta_raw, const buffer &invalid_raw);
    };

    struct required_signers_t: signer_set {
        using signer_set::signer_set;
        static required_signers_t from_cbor(cbor::zero2::value &);
    };

    struct cost_models_t {
        plutus_cost_models value {};

        static cost_models_t from_cbor(cbor::zero2::value &);
    };

    struct transaction_output_t {
        tx_out_data value {};

        static transaction_output_t from_cbor(cbor::zero2::value &);
    };

    struct transaction_outputs_t {
        tx_output_list value {};

        static transaction_outputs_t from_cbor(cbor::zero2::value &);
    };

    struct protocol_param_update_t {
        param_update value {};

        static protocol_param_update_t from_cbor(cbor::zero2::value &);
    };

    struct update_t {
        param_update_proposal_list value {};

        static update_t from_cbor(cbor::zero2::value &);
    };

    struct transaction_body_t: mary::transaction_body_t {
        required_signers_t required_signers {};
        shelley::transaction_inputs_t collateral_inputs {};

        static transaction_body_t from_cbor(cbor::zero2::value &);
    };

    struct redeemer_t {
        tx_redeemer value {};

        static redeemer_t from_cbor(cbor::zero2::value &);
    };

    struct redeemers_t {
        tx_redeemer_map items {};
        buffer raw {};

        static redeemers_t from_cbor(cbor::zero2::value &);
        void add(tx_redeemer &&);
    };

    struct transaction_witness_set_t: shelley::transaction_witness_set_t {
        redeemers_t redeemers {};

        static transaction_witness_set_t from_cbor(cbor::zero2::value &);
    };

    struct tx_base: mary::tx_base {
        using mary::tx_base::tx_base;
        virtual const signer_set &required_signers() const =0;
        virtual const input_set &collateral_inputs() const =0;
        void foreach_collateral(const std::function<void(const tx_input &)> &observer) const override;
        void foreach_required_signer(const signer_observer_t &observer) const override;
        void parse_witnesses(cbor::zero2::value &) override;
    protected:
        const tx_redeemer_map *redeemer_items() const override;
        buffer redeemer_bytes() const override;

        tx_redeemer_map _redeemers {};
        buffer _redeemers_raw {};
    };

    struct tx: tx_base {
        tx(const cardano::block_base &blk, const uint64_t blk_off, cbor::zero2::value &tx_raw, size_t idx=0, bool invalid=false);
        const tx_hash &hash() const override;
        const input_set &inputs() const override;
        const tx_output_list &outputs() const override;
        uint64_t fee() const override;
        std::optional<uint64_t> validity_end() const override;
        const withdrawal_map &withdrawals() const override;
        const cert_list &certs() const override;
        const param_update_proposal_list &updates() const override;
        buffer raw() const override;
        const multi_mint_map &mints() const override;
        std::optional<uint64_t> validity_start() const override;
        const signer_set &required_signers() const override;
        const input_set &collateral_inputs() const override;
    private:
        transaction_body_t _body;
    };

    struct block_transactions_t: block_tx_list<tx> {
        using block_tx_list<tx>::block_tx_list;
        static block_transactions_t from_cbor(const block_base &, const uint8_t *block_begin, cbor::zero2::array_reader &);
    };

    struct block: block_base {
        block(uint64_t era, uint64_t offset, uint64_t hdr_offset, cbor::zero2::value &block, const cardano::config &cfg);
        uint32_t body_size() const override;
        const cardano::block_header_base &header() const override;
        const block_hash &body_hash() const override;
        const cardano::tx_list &txs() const override;
        const invalid_tx_set &invalid_txs() const override;
    private:
        block_header _hdr;
        block_transactions_t _txs;
        block_meta_map _meta;
        invalid_tx_set _invalid_txs;
        mutable std::optional<block_hash> _body_hash {};
        const buffer _raw;

        block(uint64_t era, uint64_t offset, uint64_t hdr_offset, cbor::zero2::array_reader &it, cbor::zero2::value &blk, const cardano::config &cfg);
    };
}
