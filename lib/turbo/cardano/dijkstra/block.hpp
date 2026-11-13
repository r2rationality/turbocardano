#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/transaction.hpp>

namespace turbo::cardano::dijkstra {
    struct header_version_info_t {
        uint32_t highest_supported_major_version = 0;
        uint32_t self_reported_software_tag = 0;

        static header_version_info_t from_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;
    };

    struct eb_announcement_t {
        block_hash hash {};
        uint32_t size = 0;

        static eb_announcement_t from_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;
    };

    struct leios_certificate_t {
        uint8_vector signers {};
        byte_array<48> signature {};

        static leios_certificate_t from_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;
    };

    struct block_header_base: babbage::block_header_base {
        using babbage::block_header_base::block_header_base;
    };

    struct block_header: block_header_base {
        block_header(uint64_t era, cbor::zero2::value &, const cardano::config &);

        const block_hash &hash() const override;
        const block_hash &prev_hash() const override;
        uint64_t height() const override;
        uint64_t slot() const override;
        buffer issuer_vkey() const override;
        protocol_version protocol_ver() const override;
        const cardano::vrf_vkey &vrf_vkey() const override;
        const vrf_cert &nonce_vrf() const override;
        const vrf_cert &leader_vrf() const override;
        uint32_t body_size() const override;
        const block_hash &body_hash() const override;
        const operational_cert &op_cert() const override;
        buffer signature() const override;
        buffer raw() const override;
        buffer body_raw() const override;
        const buffer &data_raw() const override;
        void to_cbor(era_encoder &) const;

        const header_version_info_t &version_info() const;
        bool leios() const { return _leios; }
        bool body_contains_leios_certificate() const;
        const std::optional<eb_announcement_t> &eb_announcement() const;
    private:
        uint64_t _block_number = 0;
        uint64_t _slot = 0;
        std::optional<block_hash> _prev_hash{};
        vkey _issuer_vkey{};
        cardano::vrf_vkey _vrf_vkey{};
        vrf_cert _nonce_vrf{};
        uint32_t _body_size = 0;
        block_hash _body_hash{};
        operational_cert _op_cert{};
        header_version_info_t _version_info{};
        protocol_version _protocol_version {};
        bool _leios = false;
        bool _contains_leios_certificate = false;
        std::optional<eb_announcement_t> _eb_announcement{};
        buffer _body_raw{};
        byte_array<448> _signature{};
        buffer _raw{};
        mutable std::optional<block_hash> _hash{};
    };

    struct block_base: babbage::block_base {
        using babbage::block_base::block_base;
    };

    struct block_transactions_t: block_tx_list<tx> {
        using block_tx_list<tx>::block_tx_list;

        static block_transactions_t from_cbor(
            const block_base &, const uint8_t *block_begin, cbor::zero2::value &);
        void to_cbor(era_encoder &) const;
    };

    struct block: block_base {
        block(uint64_t era, uint64_t offset, uint64_t hdr_offset, cbor::zero2::value &, const cardano::config &);

        uint32_t body_size() const override;
        const block_header &header() const override;
        const block_hash &body_hash() const override;
        const tx_list &txs() const override;
        const invalid_tx_set &invalid_txs() const override;
        const std::optional<leios_certificate_t> &leios_certificate() const;
        const std::optional<uint8_vector> &peras_certificate() const;
        void body_to_cbor(era_encoder &) const;
        void to_cbor(era_encoder &) const;
    private:
        block_header _header;
        std::unique_ptr<block_transactions_t> _transactions;
        invalid_tx_set _invalid_transactions {};
        std::optional<leios_certificate_t> _leios_certificate {};
        std::optional<uint8_vector> _peras_certificate {};
        buffer _body_raw {};
        buffer _raw {};
        mutable std::optional<block_hash> _body_hash {};

        block(uint64_t era, uint64_t offset, uint64_t hdr_offset,
            cbor::zero2::array_reader &, cbor::zero2::value &, const cardano::config &);
    };
}
