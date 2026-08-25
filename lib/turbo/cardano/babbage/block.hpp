#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/alonzo/block.hpp>

namespace turbo::cardano::babbage {
    struct block_header_base: alonzo::block_header_base {
        using alonzo::block_header_base::block_header_base;
    };

    struct block_header: block_header_base {
        block_header(uint64_t era, cbor::zero2::value &hdr, const cardano::config &cfg);

        const block_hash &hash() const override
        {
            if (!_hash)
                _hash.emplace(crypto::blake2b::digest<block_hash>(_raw));
            return *_hash;
        }

        const block_hash &prev_hash() const override
        {
            return _body.prev_hash;
        }

        uint64_t height() const override
        {
            return _body.block_number;
        }

        uint64_t slot() const override
        {
            return _body.slot;
        }

        buffer issuer_vkey() const override
        {
            return _body.issuer_vkey;
        }

        protocol_version protocol_ver() const override
        {
            return _body.node_ver;
        }

        const cardano::vrf_vkey &vrf_vkey() const override
        {
            return _body.vrf_vkey;
        }

        const vrf_cert &nonce_vrf() const override
        {
            return _body.nonce_vrf;
        }

        const vrf_cert &leader_vrf() const override
        {
            return _body.nonce_vrf;
        }

        uint32_t body_size() const override
        {
            return _body.body_size;
        }

        const block_hash &body_hash() const override
        {
            return _body.body_hash;
        }

        const operational_cert &op_cert() const override
        {
            return _body.op_cert;
        }

        buffer signature() const override
        {
            return _sig;
        }

        buffer raw() const override
        {
            return _raw;
        }

        buffer body_raw() const override
        {
            return _body.raw;
        }

        const buffer &data_raw() const override
        {
            return _raw;
        }
    private:
        struct body_t {
            uint64_t block_number;
            uint64_t slot;
            block_hash prev_hash;
            vkey issuer_vkey;
            cardano::vrf_vkey vrf_vkey;
            vrf_cert nonce_vrf;
            uint32_t body_size;
            block_hash body_hash;
            operational_cert op_cert;
            protocol_version node_ver;
            buffer raw;

            body_t(cbor::zero2::value &v, const cardano::config &cfg);
            body_t(cbor::zero2::array_reader &it, cbor::zero2::value &v, const cardano::config &cfg);
        };

        body_t _body;
        buffer _sig;
        buffer _raw;
        mutable std::optional<block_hash> _hash {};

        block_header(uint64_t era, cbor::zero2::array_reader &it, cbor::zero2::value &blk, const cardano::config &cfg);
    };

    struct block_base: alonzo::block_base {
        using alonzo::block_base::block_base;
    };

    struct script_t {
        script_info value;

        static script_t from_cbor(cbor::zero2::value &);
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

    struct transaction_body_t: alonzo::transaction_body_t {
        shelley::transaction_inputs_t ref_inputs {};
        std::optional<tx_output> collateral_return {};
        std::optional<uint64_t> collateral_value {};

        static transaction_body_t from_cbor(cbor::zero2::value &);
    };

    struct transaction_witness_set_t: alonzo::transaction_witness_set_t {
        static transaction_witness_set_t from_cbor(cbor::zero2::value &);
    };

    struct tx_base: alonzo::tx_base {
        using alonzo::tx_base::tx_base;
        virtual const input_set &ref_inputs() const =0;
        virtual const std::optional<tx_output> &collateral_return() const =0;
        virtual const std::optional<uint64_t> &collateral_value() const =0;
        void foreach_referenced_input(const input_observer_t &observer) const override;
        void parse_witnesses(cbor::zero2::value &) override;
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
        const input_set &ref_inputs() const override;
        const std::optional<tx_output> &collateral_return() const override;
        const std::optional<uint64_t> &collateral_value() const override;
    private:
        transaction_body_t _body;
    };

    struct block_transactions_t: block_tx_list<tx> {
        using block_tx_list<tx>::block_tx_list;
        static block_transactions_t from_cbor(const block_base &, const uint8_t *block_begin, cbor::zero2::array_reader &);
    };

    struct block: block_base {
        block(uint64_t era, uint64_t offset, uint64_t hdr_offset, cbor::zero2::value &block_tuple, const cardano::config &cfg);
        uint32_t body_size() const override;
        const block_header_base &header() const override;
        const block_hash &body_hash() const override;
        const tx_list &txs() const override;
        const invalid_tx_set &invalid_txs() const override;
    private:
        block_header _hdr;
        block_transactions_t _txs;
        block_meta_map _meta;
        invalid_tx_set _invalid_txs;
        mutable std::optional<block_hash> _body_hash {};
        const buffer _raw;

        block(uint64_t era, uint64_t offset, uint64_t hdr_offset, cbor::zero2::array_reader &it, cbor::zero2::value &block_tuple, const cardano::config &cfg);
    };
}
