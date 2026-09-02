#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/common.hpp>

namespace turbo::cardano::shelley {
    struct native_script_t {
        static native_script_t from_cbor(cbor::zero2::value &);
        static void validate_cbor(cbor::zero2::value &);
    };

    struct block_header_base: cardano::block_header_base {
        using cardano::block_header_base::block_header_base;
        static buffer prev_hash_from_cbor(
            cbor::zero2::value &v, const cardano::config &cfg, bool *is_null=nullptr);
        virtual const cardano::vrf_vkey &vrf_vkey() const =0;
        virtual const vrf_cert &nonce_vrf() const =0;
        virtual const vrf_cert &leader_vrf() const =0;
        virtual uint32_t body_size() const =0;
        virtual const block_hash &body_hash() const =0;
        virtual const operational_cert &op_cert() const =0;
        virtual buffer signature() const =0;
        virtual buffer raw() const =0;
        virtual buffer body_raw() const =0;
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
            return _body.leader_vrf;
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
            uint32_t block_number;
            uint32_t slot;
            block_hash prev_hash;
            vkey issuer_vkey;
            cardano::vrf_vkey vrf_vkey;
            vrf_cert nonce_vrf;
            vrf_cert leader_vrf;
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

        block_header(uint64_t era, cbor::zero2::array_reader &it, cbor::zero2::value &hdr, const cardano::config &cfg);
    };

    struct block_base: cardano::block_base {
        using cardano::block_base::block_base;
        static block_hash compute_body_hash(const buffer &txs_raw, const buffer &wits_raw, const buffer &meta_raw);
        virtual const block_hash &body_hash() const =0;
        const block_kes_signature kes() const override;
        const block_vrf vrf() const override;
        bool body_hash_ok() const override;
        bool signature_ok() const override;
        void foreach_update_proposal(const std::function<void(const param_update_proposal &)> &observer) const override;
    };

    struct transaction_input_t {
        tx_out_ref value {};

        static transaction_input_t from_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;
    };

    struct transaction_inputs_t: input_set {
        using input_set::input_set;
        static transaction_inputs_t from_cbor(cbor::zero2::value &);
    };

    struct transaction_output_t {
        tx_out_data value {};

        static transaction_output_t from_cbor(cbor::zero2::value &);
    };

    struct transaction_outputs_t: tx_output_list {
        using tx_output_list::tx_output_list;
        static transaction_outputs_t from_cbor(cbor::zero2::value &);
    };

    struct certificate_t {
        cardano::cert_t value {};

        static certificate_t from_cbor(cbor::zero2::value &);
    };

    struct certificates_t: cert_list {
        using cert_list::cert_list;
        static certificates_t from_cbor(cbor::zero2::value &);
    };

    struct withdrawals_t: withdrawal_map {
        using withdrawal_map::withdrawal_map;
        static withdrawals_t from_cbor(cbor::zero2::value &);
    };

    struct protocol_param_update_t {
        param_update value {};

        static protocol_param_update_t from_cbor(cbor::zero2::value &);
    };

    struct update_t: param_update_proposal_list {
        using param_update_proposal_list::param_update_proposal_list;
        static update_t from_cbor(cbor::zero2::value &);
    };

    struct transaction_body_t {
        transaction_inputs_t inputs {};
        transaction_outputs_t outputs {};
        uint64_t fee = 0;
        std::optional<uint64_t> validity_end {};
        certificates_t certs {};
        withdrawals_t withdrawals {};
        update_t updates {};
        buffer raw {};
        mutable std::optional<tx_hash> hash {};

        static transaction_body_t from_cbor(cbor::zero2::value &);
    };

    struct transaction_witness_set_t {
        tx_wit_list items {};
        buffer raw {};

        static transaction_witness_set_t from_cbor(cbor::zero2::value &);
    };

    struct tx_base: cardano::tx_base {
        using cardano::tx_base::tx_base;
        virtual const withdrawal_map &withdrawals() const =0;
        virtual const param_update_proposal_list &updates() const =0;
        void foreach_param_update(const update_observer_t &observer) const override;
        void foreach_withdrawal(const withdrawal_observer_t &observer) const override;
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
        const block_header_base &header() const override;
        const block_hash &body_hash() const override;
        const tx_list &txs() const override;
    private:
        block_header _hdr;
        block_transactions_t _txs;
        block_meta_map _meta;
        mutable std::optional<block_hash> _body_hash {};
        const buffer _raw;

        block(uint64_t era, uint64_t offset, uint64_t hdr_offset, cbor::zero2::array_reader &it, cbor::zero2::value &block, const cardano::config &cfg);
    };
}
