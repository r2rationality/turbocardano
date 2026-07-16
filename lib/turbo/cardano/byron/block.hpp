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
            memcpy(padded.data() + 2, data.data(), data.size());
            return crypto::blake2b::digest<block_hash>(padded);
        }

        boundary_block_header(const uint64_t era, cbor::zero2::value &hdr, const cardano::config &cfg):
            boundary_block_header(era, hdr.array(), hdr, cfg)
        {
        }

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

        boundary_block_header(const uint64_t era, cbor::zero2::array_reader &it, cbor::zero2::value &hdr, const cardano::config &cfg):
            block_header_base { era, cfg },
            _prev_hash { it.skip(1).read().bytes() },
            _slot { it.skip(1).read().array().read().uint() * cfg.byron_epoch_length },
            _hash { padded_hash(0x00, hdr.data_raw()) },
            _hdr_raw { hdr.data_raw() }
        {
        }
    };

    struct boundary_block: cardano::block_base {
        boundary_block(const uint64_t era, const uint64_t offset, const uint64_t hdr_offset, cbor::zero2::value &block, const cardano::config &cfg):
            boundary_block(era, offset, hdr_offset, block.array(), block, cfg)
        {
        }

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

            static tx_proof_t from_cbor(cbor::zero2::value &v)
            {
                auto &it = v.array();
                return { it.read().uint(), it.read().bytes(), it.read().bytes() };
            }
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
        block_header(uint64_t era, cbor::zero2::value &hdr, const cardano::config &cfg):
            block_header { era, hdr.array(), hdr, cfg }
        {
        }

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

            protocol_magic_t(cbor::zero2::value &v):
                magic { v.uint() },
                magic_raw { v.data_raw() }
            {
            }
        };

        struct extra_t {
            const buffer raw;

            extra_t(cbor::zero2::value &v):
                raw { v.data_raw() }
            {
            }
        };

        struct slot_id_t {
            uint64_t epoch;
            uint64_t epoch_slot;
            buffer raw;

            static slot_id_t from_cbor(cbor::zero2::value &v)
            {
                auto &it = v.array();
                return { it.read().uint(), it.read().uint(), v.data_raw() };
            }

            uint64_t slot(const cardano::config &cfg=cardano::config::get()) const noexcept
            {
                return epoch * cfg.byron_epoch_length + epoch_slot;
            }
        };

        struct byron_vkey_t {
            crypto::ed25519::vkey_full vkey_full;

            static byron_vkey_t from_cbor(cbor::zero2::value &v)
            {
                return { v.bytes() };
            }

            buffer vkey() const
            {
                return static_cast<buffer>(vkey_full).subspan(0, sizeof(crypto::ed25519::vkey));
            }
        };

        struct byron_block_sig_t {
            struct delegate_sig_t {
                uint64_t epoch;
                byron_vkey_t issuer;
                byron_vkey_t dlg;
                crypto::ed25519::signature cert;
                crypto::ed25519::signature sig;

                static delegate_sig_t from_cbor(cbor::zero2::value &v)
                {
                    auto &it = v.array();
                    auto &dlg = it.read();
                    auto &d_it = dlg.array();
                    return {
                        d_it.read().uint(),
                        { d_it.read().bytes() },
                        { d_it.read().bytes() },
                        d_it.read().bytes(),
                        it.read().bytes()
                    };
                }
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
        };

        struct consensus_t {
            slot_id_t slotid;
            byron_vkey_t vkey;
            uint64_t difficulty;
            byron_block_sig_t sig;
            const buffer raw;

            static consensus_t from_cbor(cbor::zero2::value &v) {
                auto &it = v.array();
                return {
                    decltype(slotid)::from_cbor(it.read()),
                    decltype(vkey)::from_cbor(it.read()),
                    it.read().array().read().uint(),
                    decltype(sig)::from_cbor(it.read()),
                    v.data_raw()
                };
            }
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
        using input_list = std::vector<tx_input>;
        // Byron inputs must be kept as is to match the witnesses!
        input_list _inputs;
        tx_output_list _outputs;
        buffer _raw;
        mutable std::optional<tx_hash> _hash {};

        static input_list parse_inputs(cbor::zero2::value &);
        static tx_output_list parse_outputs(cbor::zero2::value &);
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

            static tx_list parse_txs(const block &, const uint8_t *block_begin, cbor::zero2::value &v);

            tx_list(std::vector<tx> &&txs);
        };

        struct ssc_payload_t {
            const buffer raw;
            ssc_payload_t(cbor::zero2::value &v):
                raw { v.data_raw() }
            {
            }
        };

        struct dlg_payload_t {
            const buffer raw;
            dlg_payload_t(cbor::zero2::value &v):
                raw { v.data_raw() }
            {
            }
        };

        struct upd_payload_t {
            std::vector<param_update_proposal> proposals {};
            std::vector<param_update_vote> votes {};
            buffer raw;

            upd_payload_t(const block &blk, cbor::zero2::value &v);
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