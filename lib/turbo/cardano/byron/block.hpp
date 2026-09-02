#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/common.hpp>

namespace turbo::cardano::byron {
    struct boundary_block_header: block_header_base {
        static block_hash padded_hash(const uint8_t magic, const buffer data)
        {
            uint8_vector padded(data.size() + 2);
            padded[0] = 0x82;
            padded[1] = magic;
            if (!data.empty())
                memcpy(padded.data() + 2, data.data(), data.size());
            return crypto::blake2b::digest<block_hash>(padded);
        }

        boundary_block_header(uint64_t era, cbor::zero2::value &hdr, const cardano::config &cfg);

        const buffer &data_raw() const override
        {
            return _hdr_raw;
        }

        uint64_t height() const override
        {
            return slot();
        }

        const block_hash &hash() const override
        {
            return _hash;
        }

        const block_hash &prev_hash() const override
        {
            return _prev_hash;
        }

        uint64_t slot() const override
        {
            return _slot;
        }

        protocol_version protocol_ver() const override
        {
            return { 1, 0 };
        }

        buffer issuer_vkey() const override
        {
            static auto dummy = vkey::from_hex("0000000000000000000000000000000000000000000000000000000000000000");
            return dummy;
        }
    private:
        const block_hash _prev_hash;
        const uint64_t _slot;
        const block_hash _hash;
        const buffer _hdr_raw;

        boundary_block_header(uint64_t era, cbor::zero2::array_reader &it, cbor::zero2::value &hdr, const cardano::config &cfg);
    };

    struct boundary_block: cardano::block_base {
        boundary_block(uint64_t era, uint64_t offset, uint64_t hdr_offset, cbor::zero2::value &block, const cardano::config &cfg);

        uint32_t body_size() const override
        {
            // boundary blocks have not body!
            return 0;
        }

        const block_header_base &header() const override
        {
            return _hdr;
        }

        const tx_list &txs() const override
        {
            // return an empty list for compatibility
            return _txs;
        }

        bool signature_ok() const override
        {
            return true;
        }
    private:
        boundary_block_header _hdr;
        tx_list _txs;
        const buffer _raw;

        boundary_block(const uint64_t era, const uint64_t offset, const uint64_t hdr_offset, cbor::zero2::array_reader &it, cbor::zero2::value &block, const cardano::config &cfg);
    };

    struct proof_data_t {
        struct tx_proof_t {
            size_t tx_count;
            block_hash tx_merkle_root;
            block_hash tx_wits_hash;

            static tx_proof_t from_cbor(cbor::zero2::value &);
        };

        tx_proof_t tx_proof;
        block_hash dlg_hash;
        block_hash upd_hash;

        bool operator==(const proof_data_t &o) const noexcept;
    };

    struct proof_data_extended_t {
        proof_data_t proof;
        buffer raw;

        static proof_data_extended_t from_cbor(cbor::zero2::value &);
        bool operator==(const proof_data_t &o) const noexcept;
    };

    struct block_header: block_header_base {
        block_header(uint64_t era, cbor::zero2::value &hdr, const cardano::config &cfg);

        const buffer &data_raw() const override
        {
            return _hdr_raw;
        }

        uint64_t height() const override
        {
            return slot();
        }

        const block_hash &hash() const override
        {
            return _hash;
        }

        const block_hash &prev_hash() const override
        {
            return _prev_hash;
        }

        uint64_t slot() const override
        {
            return _consensus.slotid.slot(_cfg);
        }

        protocol_version protocol_ver() const override
        {
            return { 1, 0 };
        }

        buffer issuer_vkey() const override
        {
            return _consensus.vkey.vkey();
        }

        buffer delegate_vkey() const
        {
            return _consensus.sig.delegate_vkey();
        }

        uint64_t delegation_epoch() const
        {
            return _consensus.sig.certificate().epoch;
        }

        uint64_t protocol_magic() const
        {
            return _protocol_magic.magic;
        }

        bool delegation_certificate_matches(const cardano::byron_delegate_info &) const;

        const buffer signature() const
        {
            return _consensus.sig.signature();
        }

        const buffer signed_data() const
        {
            if (!_signed_data) {
                _signed_data.emplace(_make_signed_data());
            }
            return *_signed_data;
        }

        const proof_data_t &proof() const
        {
            return _proof.proof;
        }

        buffer protocol_magic_raw() const override
        {
            return _protocol_magic.magic_raw;
        }
    private:
        struct protocol_magic_t {
            const uint64_t magic;
            const buffer magic_raw;

            static protocol_magic_t from_cbor(cbor::zero2::value &);
        };

        struct extra_t {
            const buffer raw;

            static extra_t from_cbor(cbor::zero2::value &);
        };

        struct slot_id_t {
            uint64_t epoch;
            uint64_t epoch_slot;
            buffer raw;

            static slot_id_t from_cbor(cbor::zero2::value &);

            uint64_t slot(const cardano::config &cfg=cardano::config::get()) const noexcept
            {
                return epoch * cfg.byron_epoch_length + epoch_slot;
            }
        };

        struct byron_vkey_t {
            crypto::ed25519::vkey_full vkey_full;

            static byron_vkey_t from_cbor(cbor::zero2::value &);

            buffer vkey() const
            {
                return static_cast<buffer>(vkey_full).subspan(0, sizeof(crypto::ed25519::vkey));
            }
        };

        struct byron_block_sig_t {
            struct delegate_sig_t {
                uint64_t epoch;
                buffer epoch_raw;
                byron_vkey_t issuer;
                byron_vkey_t dlg;
                crypto::ed25519::signature cert;
                crypto::ed25519::signature sig;

                static delegate_sig_t from_cbor(cbor::zero2::value &);
            };

            using value_type = std::variant<crypto::ed25519::signature, delegate_sig_t>;
            value_type val;

            static byron_block_sig_t from_cbor(cbor::zero2::value &v);

            buffer signature() const
            {
                return variant::get_nice<delegate_sig_t>(val).sig;
            }

            buffer delegate_vkey() const
            {
                return variant::get_nice<delegate_sig_t>(val).dlg.vkey();
            }

            const delegate_sig_t &certificate() const
            {
                return variant::get_nice<delegate_sig_t>(val);
            }
        };

        struct consensus_t {
            slot_id_t slotid;
            byron_vkey_t vkey;
            uint64_t difficulty;
            byron_block_sig_t sig;
            const buffer raw;

            static consensus_t from_cbor(cbor::zero2::value &);
        };

        protocol_magic_t _protocol_magic;
        const block_hash _prev_hash;
        const proof_data_extended_t _proof;
        const consensus_t _consensus;
        const extra_t _extra;
        const buffer _hdr_raw;
        const block_hash _hash;
        mutable std::optional<uint8_vector> _signed_data;

        block_header(uint64_t era, cbor::zero2::array_reader &it, cbor::zero2::value &hdr, const cardano::config &cfg);
        uint8_vector _make_signed_data() const;
    };

    struct transaction_inputs_t: std::vector<tx_input> {
        using std::vector<tx_input>::vector;
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

    struct transaction_body_t {
        transaction_inputs_t inputs {};
        transaction_outputs_t outputs {};
        buffer raw {};

        static transaction_body_t from_cbor(cbor::zero2::value &);
    };

    struct transaction_witness_set_t {
        tx_wit_list items {};
        buffer raw {};

        static transaction_witness_set_t from_cbor(cbor::zero2::value &);
    };

    struct tx: tx_base {
        tx(const cardano::block_base &blk, uint64_t blk_off, cbor::zero2::value &tx_raw, size_t idx=0, bool invalid=false);
        void foreach_input(const input_observer_t &) const override; // needs to be virtual since byron inputs are unordered and need special handling
        const cert_list &certs() const override;
        const tx_hash &hash() const override;
        const input_set &inputs() const override;
        const tx_output_list &outputs() const override;
        void parse_witnesses(cbor::zero2::value &) override;
        uint64_t fee() const override;
        buffer raw() const override;
    private:
        transaction_body_t _body;
        mutable std::optional<tx_hash> _hash {};
    };

    struct block: cardano::block_base {
        block(uint64_t era, uint64_t offset, const uint64_t hdr_offset, cbor::zero2::value &blk, const cardano::config &cfg);
        uint32_t body_size() const override;
        const block_header_base &header() const override;
        const tx_list &txs() const override;
        void foreach_update_proposal(const std::function<void(const param_update_proposal &)> &observer) const override;
        void foreach_update_vote(const std::function<void(const param_update_vote &)> &observer) const override;
        bool signature_ok() const override;
        bool body_hash_ok() const override;
    private:
        struct tx_list {
            std::vector<tx> txs;
            cardano::tx_list txs_view;

            static tx_list from_cbor(const block &, const uint8_t *block_begin, cbor::zero2::value &);

            tx_list(std::vector<tx> &&txs);
        };

        struct ssc_payload_t {
            const buffer raw;
            static ssc_payload_t from_cbor(cbor::zero2::value &);
        };

        struct dlg_payload_t {
            const buffer raw;
            static dlg_payload_t from_cbor(cbor::zero2::value &);
        };

        struct upd_payload_t {
            std::vector<param_update_proposal> proposals {};
            std::vector<param_update_vote> votes {};
            buffer raw;

            static upd_payload_t from_cbor(const block &, cbor::zero2::value &);
        };

        struct body_t {
            tx_list txs;
            ssc_payload_t sscs;
            dlg_payload_t dlgs;
            upd_payload_t updates;

            static body_t from_cbor(const block &blk, const uint8_t *block_begin, cbor::zero2::value &v);
        };

        block_header _hdr;
        body_t _body;
        proof_data_t _proof_actual;
        const buffer _raw;

        static proof_data_t compute_proof_data(const cardano::tx_list &txs, const buffer &dlg_raw, const buffer &upd_raw);
        block(uint64_t era, uint64_t offset, const uint64_t hdr_offset, cbor::zero2::array_reader &it, cbor::zero2::value &blk, const cardano::config &cfg);
    };
}
