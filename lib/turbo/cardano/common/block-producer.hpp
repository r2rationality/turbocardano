#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cardano/common/common.hpp>
#include <turbo/cardano/kes.hpp>
#include <turbo/cardano/shelley/block.hpp>
#include <turbo/cardano/vrf.hpp>
#include <turbo/cbor/encoder.hpp>

namespace turbo::cardano {
    // This class is intended to generate both valid invalid blocks as part of the testing process.
    // For this reason, it does not enforce any invariants beyond the ones necessary to satisfy the CBOR format.
    struct block_producer {
        struct tx_input {
            cardano::tx_hash tx_hash {};
            cardano::tx_out_idx txo_idx {};
            crypto::ed25519::skey sk {};
            crypto::ed25519::vkey vk {};
        };
        using tx_input_list = std::vector<tx_input>;

        struct tx_output {
            uint8_vector address {};
            uint64_t coin = 0;
        };
        using tx_output_list = std::vector<tx_output>;

        struct tx {
            tx_input_list inputs {};
            tx_output_list outputs {};
        };
        using tx_list = std::vector<tx>;

        uint64_t height {};
        uint64_t slot = 0;
        block_hash prev_hash {};
        uint64_t op_seq_no {};
        tx_list txs {};
        cardano::vrf_nonce vrf_nonce = cardano::vrf_nonce::from_hex("1a3be38bcbb7911969283716ad7aa550250226b76a61fc51cc9a9a35d9276d81");

        explicit block_producer(const buffer &cold_key, const buffer &kes_seed, const buffer &vrf_sk)
            : _cold_sk { cold_key }, _cold_vk { crypto::ed25519::extract_vk(_cold_sk) }, _kes_sk { kes_seed },
                _vrf_sk { vrf_sk }, _vrf_vk { vrf03_extract_vk(_vrf_sk) }
        {
        }

        [[nodiscard]] uint8_vector cbor() const {
            const auto [tx_bodies, tx_wits] = _gen_transactions();
            const auto data = _gen_data();
            const auto inval = _gen_invalid();
            cbor::encoder enc {};
            enc.array(2);
            enc.uint(6);
            enc.array(5);
            _gen_header(enc, tx_bodies, tx_wits, data, inval);
            enc.cbor() << tx_bodies << tx_wits << data << inval;
            return enc.cbor();
        }
    private:
        crypto::ed25519::skey _cold_sk;
        crypto::ed25519::vkey _cold_vk;
        kes::secret<6> _kes_sk;
        vrf_skey _vrf_sk;
        vrf_vkey _vrf_vk;

        void _gen_op_cert(cbor::encoder &enc) const
        {
            const auto &op_vkey = _kes_sk.vkey();
            byte_array<sizeof(vkey) + 2 * sizeof(uint64_t)> ocert_data {};
            memcpy(ocert_data.data(), op_vkey.data(), op_vkey.size());
            const auto ctr = host_to_net<uint64_t>(op_seq_no);
            memcpy(ocert_data.data() + op_vkey.size(), &ctr, sizeof(uint64_t));
            const auto kp = host_to_net<uint64_t>(op_seq_no);
            memcpy(ocert_data.data() + op_vkey.size() + sizeof(uint64_t), &kp, sizeof(uint64_t));
            crypto::ed25519::signature op_signature {};
            crypto::ed25519::sign(op_signature, ocert_data, _cold_sk);
            const auto op_period = slot / cardano::shelley::kes_period_slots;
            enc.array(4);
            enc.bytes(op_vkey);
            enc.uint(op_seq_no);
            enc.uint(op_period);
            enc.bytes(op_signature);
        }

        void _gen_protocol_ver(cbor::encoder &enc) const
        {
            enc.array(2);
            enc.uint(8);
            enc.uint(0);
        }

        void _gen_body_hash(cbor::encoder &enc, const buffer &txs, const buffer &wits, const buffer &data, const buffer &inval) const
        {
            std::array<block_hash, 4> hashes {};
            crypto::blake2b::digest(hashes[0], txs);
            crypto::blake2b::digest(hashes[1], wits);
            crypto::blake2b::digest(hashes[2], data);
            crypto::blake2b::digest(hashes[3], inval);
            enc.bytes(crypto::blake2b::digest<block_hash>(buffer { reinterpret_cast<uint8_t*>(hashes.data()), hashes.size() * sizeof(hashes[0]) }));
        }

        void _gen_vrf_result(cbor::encoder &enc) const
        {
            const auto input = vrf_make_input(slot, vrf_nonce);
            vrf_result res {};
            vrf_proof proof {};
            vrf03_prove(proof, res, _vrf_sk, input);
            enc.array(2);
            enc.bytes(res);
            enc.bytes(proof);
        }

        void _gen_header_body(cbor::encoder &enc, const buffer &txs, const buffer &wits, const buffer &data, const buffer &inval) const
        {
            enc.array(10);
            enc.uint(height);
            enc.uint(slot);
            enc.bytes(prev_hash);
            enc.bytes(_cold_vk);
            enc.bytes(_vrf_vk);
            _gen_vrf_result(enc);
            enc.uint(txs.size() + wits.size() + data.size() + inval.size());
            _gen_body_hash(enc, txs, wits, data, inval);
            _gen_op_cert(enc);
            _gen_protocol_ver(enc);
        }

        void _gen_kes_signature(cbor::encoder &enc, const buffer &header_body_cbor) const
        {
            cardano_kes_signature_data sigma {};
            _kes_sk.sign(sigma, header_body_cbor);
            enc.bytes(sigma);
        }

        void _gen_header(cbor::encoder &enc, const buffer &txs, const buffer &wits, const buffer &data, const buffer &inval) const
        {
            enc.array(2);
            cbor::encoder hb_enc {};
            _gen_header_body(hb_enc, txs, wits, data, inval);
            enc << hb_enc;
            _gen_kes_signature(enc, hb_enc.cbor());
        }

        [[nodiscard]] std::pair<uint8_vector, uint8_vector> _gen_transactions() const
        {
            cbor::encoder enc {};
            cbor::encoder wit_enc {};
            enc.array(txs.size());
            wit_enc.array(txs.size());
            for (const auto &tx: txs) {
                std::map<crypto::ed25519::vkey, crypto::ed25519::skey> keys {};
                cbor::encoder tx_enc {};
                tx_enc.map(2);
                tx_enc.uint(0).array(tx.inputs.size());
                for (const auto &tx_in: tx.inputs) {
                    tx_enc.array(2)
                        .bytes(tx_in.tx_hash)
                        .uint(tx_in.txo_idx);
                    keys.try_emplace(tx_in.vk, tx_in.sk);
                }
                tx_enc.uint(1).array(tx.outputs.size());
                for (const auto &tx_out: tx.outputs) {
                    tx_enc.map(2)
                        .uint(0).bytes(tx_out.address)
                        .uint(1).uint(tx_out.coin);
                }
                const auto tx_hash = crypto::blake2b::digest<cardano::tx_hash>(tx_enc.cbor());
                wit_enc.map(1);
                wit_enc.uint(0).array(keys.size());
                for (const auto &[vk, sk]: keys) {
                    wit_enc.array(2).bytes(vk).bytes(crypto::ed25519::sign(tx_hash, sk));
                }
                enc << tx_enc;
            }
            return std::make_pair(enc.cbor(), wit_enc.cbor());
        }

        [[nodiscard]] uint8_vector _gen_data() const
        {
            cbor::encoder enc {};
            enc.map(0);
            return enc.cbor();
        }

        [[nodiscard]] uint8_vector _gen_invalid() const
        {
            cbor::encoder enc {};
            enc.array(0);
            return enc.cbor();
        }
    };
}