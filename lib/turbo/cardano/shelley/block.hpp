#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/common.hpp>

namespace turbo::cardano::shelley {
    static constexpr uint64_t kes_period_slots = 129600;

    struct block_header_base: cardano::block_header_base {
        using cardano::block_header_base::block_header_base;
        static buffer prev_hash_from_cbor(cbor::zero2::value &v, const cardano::config &cfg);
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
    protected:
        template<typename TX>
        static block_tx_list<TX> parse_txs(const block_base &blk, const uint8_t *block_begin, cbor::zero2::array_reader &block_it)
        {
            decltype(block_tx_list<TX>::txs) txs {};
            buffer txs_cbor;
            {
                auto &txs_raw = block_it.read();
                if (!txs_raw.indefinite()) [[likely]]
                    txs.reserve(txs_raw.special_uint());
                auto &it = txs_raw.array();
                while (!it.done()) {
                    auto &tx = it.read();
                    txs.emplace_back(blk, tx.data_begin() - block_begin, tx, txs.size());
                }
                txs_cbor = txs_raw.data_raw();
            }
            auto &wits_raw = block_it.read();
            auto &it = wits_raw.array();
            for (size_t i = 0; !it.done(); ++i) {
                txs.at(i).parse_witnesses(it.read());
            }
            return { std::move(txs), txs_cbor, wits_raw.data_raw() };
        }
    };

    struct tx_base: cardano::tx_base {
        using cardano::tx_base::tx_base;
        virtual const withdrawal_map &withdrawals() const =0;
        virtual const param_update_proposal_list &updates() const =0;
        void foreach_param_update(const update_observer_t &observer) const override;
        void foreach_withdrawal(const withdrawal_observer_t &observer) const override;
        void parse_witnesses(cbor::zero2::value &) override;
    protected:
        static input_set parse_inputs(cbor::zero2::value &);
        static tx_output_list parse_outputs(cbor::zero2::value &);
        static cert_list parse_certs(cbor::zero2::value &);
        static withdrawal_map parse_withdrawals(cbor::zero2::value &);
        static param_update_proposal_list parse_updates(cbor::zero2::value &);
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
    protected:
        input_set _inputs {};
        tx_output_list _outputs {};
        uint64_t _fee = 0;
        std::optional<uint64_t> _validity_end {};
        cert_list _certs {};
        withdrawal_map _withdrawals {};
        param_update_proposal_list _updates {};
        buffer _raw;
        mutable std::optional<tx_hash> _hash {};
    };

    struct block: block_base {
        block(uint64_t era, uint64_t offset, uint64_t hdr_offset, cbor::zero2::value &block, const cardano::config &cfg);
        uint32_t body_size() const override;
        const block_header_base &header() const override;
        const block_hash &body_hash() const override;
        const tx_list &txs() const override;
    private:
        block_header _hdr;
        block_tx_list<tx> _txs;
        block_meta_map _meta;
        mutable std::optional<block_hash> _body_hash {};
        const buffer _raw;

        block(uint64_t era, uint64_t offset, uint64_t hdr_offset, cbor::zero2::array_reader &it, cbor::zero2::value &block, const cardano::config &cfg);
    };
}