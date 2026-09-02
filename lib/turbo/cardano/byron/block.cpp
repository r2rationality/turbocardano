/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/byron/block.hpp>

namespace turbo::cardano::byron {
    tx::tx(const cardano::block_base &blk, const uint64_t blk_off, cbor::zero2::value &tx_raw, const size_t idx, const bool invalid):
        tx_base { blk, blk_off, idx, invalid },
        _body { transaction_body_t::from_cbor(tx_raw) }
    {
    }

    void tx::parse_witnesses(cbor::zero2::value &v)
    {
        auto decoded = transaction_witness_set_t::from_cbor(v);
        if (decoded.items.size() != _body.inputs.size()) [[unlikely]]
            throw error(fmt::format("slot: {} tx: {}: #wits: {} != #inputs: {}", _blk.slot_object(), hash(), decoded.items.size(), _body.inputs.size()));
        _wits = std::move(decoded.items);
        _wits_raw = decoded.raw;
    }

    static crypto::blake2b::hash_32 merkle_leaf_hash(const buffer tx_raw)
    {
        uint8_vector data {};
        data << 0 << tx_raw;
        return crypto::blake2b::digest(data);
    }

    static crypto::blake2b::hash_32 merkle_node_hash(const crypto::blake2b::hash_32 &l, const crypto::blake2b::hash_32 &r)
    {
        uint8_vector data {};
        data << 1 << l << r;
        return crypto::blake2b::digest(data);
    }

    static tx_hash make_merkle_tree_root(const tx_list &txs)
    {
        using merkle_level = std::vector<tx_hash>;
        using merkle_tree = std::vector<merkle_level>;

        if (!txs.empty()) {
            merkle_tree mt {};
            mt.emplace_back();
            {
                auto &level0 = mt.back();
                for (const auto &tx: txs)
                    level0.emplace_back(merkle_leaf_hash(tx->raw()));
            }

            while (mt.back().size() > 1) {
                mt.emplace_back();
                auto &next_level = mt.back();
                auto &prev_level = mt.at(mt.size() - 2);
                for (size_t i = 0; i < prev_level.size(); i += 2) {
                    if (i + 1 < prev_level.size()) [[likely]] {
                        next_level.emplace_back(merkle_node_hash(prev_level[i], prev_level[i + 1]));
                    } else {
                        next_level.emplace_back(prev_level[i]);
                    }
                }
            }

            return mt.back().at(0);
        }

        return crypto::blake2b::digest<tx_hash>(uint8_vector {});
    }

    bool proof_data_t::operator==(const proof_data_t &o) const noexcept
    {
        // use binary & to eliminate unnecessary branching
        return static_cast<int>(tx_proof.tx_count == o.tx_proof.tx_count)
            & static_cast<int>(tx_proof.tx_merkle_root == o.tx_proof.tx_merkle_root)
            & static_cast<int>(tx_proof.tx_wits_hash == o.tx_proof.tx_wits_hash)
            & static_cast<int>(dlg_hash == o.dlg_hash)
            & static_cast<int>(upd_hash == o.upd_hash);
    }

    uint8_vector block_header::_make_signed_data() const
    {
        using namespace std::literals;
        cbor::encoder enc {};
        enc.cbor().reserve(512);
        enc.cbor() << "01"sv;
        enc.cbor() << _consensus.vkey.vkey_full;
        enc.cbor() << "\x09"sv;
        enc.cbor() << _protocol_magic.magic_raw;
        enc.cbor() << "\x85"sv; // CBOR Array of length 5
        enc.bytes(_prev_hash);
        enc.cbor() << _proof.raw;
        enc.cbor() << _consensus.slotid.raw;
        enc.array(1);
        enc.uint(_consensus.difficulty);
        enc.cbor() << _extra.raw;
        return std::move(enc.cbor());
    }

    bool block_header::delegation_certificate_matches(const cardano::byron_delegate_info &expected) const
    {
        const auto &cert = _consensus.sig.certificate();
        return _consensus.vkey.vkey_full == cert.issuer.vkey_full
            && cert.issuer.vkey_full == expected.issuer
            && cert.dlg.vkey_full == expected.delegate
            && cert.cert == expected.certificate
            && cert.epoch == expected.epoch
            && cert.epoch_raw == expected.epoch_cbor;
    }

    uint64_t tx::fee() const
    {
        throw error("byron::tx requires access to the utxo set to compute the tx fee!");
    }

    const cert_list &tx::certs() const
    {
        static cert_list empty {};
        return empty;
    }

    const tx_hash &tx::hash() const
    {
        if (!_hash)
            _hash.emplace(crypto::blake2b::digest<tx_hash>(_body.raw));
        return *_hash;
    }

    const input_set &tx::inputs() const
    {
        throw error("byron inputs are unordered - cannot returned an order input set - use foreach_input instead!");
    }

    void tx::foreach_input(const input_observer_t &observer) const
    {
        for (const auto &txi: _body.inputs)
            observer(txi);
    }

    const tx_output_list &tx::outputs() const
    {
        return _body.outputs;
    }

    buffer tx::raw() const
    {
        return _body.raw;
    }

    proof_data_t block::compute_proof_data(const cardano::tx_list &txs, const buffer &dlg_raw, const buffer &upd_raw)
    {
        cbor::encoder tx_wits_enc {};
        tx_wits_enc.array();
        for (const auto &tx: txs)
            tx_wits_enc << tx->witness_raw();
        tx_wits_enc.s_break();
        return {
            proof_data_t::tx_proof_t {
                txs.size(),
                make_merkle_tree_root(txs),
                crypto::blake2b::digest(tx_wits_enc.cbor()),
            },
            crypto::blake2b::digest(dlg_raw),
            crypto::blake2b::digest(upd_raw)
        };
    }

    block::tx_list::tx_list(std::vector<tx> &&new_txs):
        txs { std::move(new_txs) }
    {
        txs_view.reserve(txs.size());
        for (auto &tx: txs)
            txs_view.emplace_back(&tx);
    }

    uint32_t block::body_size() const
    {
        return numeric_cast<uint32_t>(_raw.size());
    }

    const block_header_base &block::header() const
    {
        return _hdr;
    }

    const tx_list &block::txs() const
    {
        return _body.txs.txs_view;
    }

    void block::foreach_update_vote(const std::function<void(const param_update_vote &)> &observer) const
    {
        for (const auto &vote: _body.updates.votes)
            observer(vote);
    }

    bool block::signature_ok() const
    {
        const auto &cfg = _hdr.config();
        // Mainnet Byron blocks retained the heavy-delegation certificates
        // pinned by genesis; dynamic delegation is intentionally unsupported.
        const auto expected = cfg.byron_delegates.find(_hdr.issuer_vkey());
        return expected != cfg.byron_delegates.end()
            && _hdr.protocol_magic() == cfg.byron_protocol_magic
            && _hdr.delegation_certificate_matches(expected->second)
            && _hdr.delegation_epoch() <= _hdr.slot() / cfg.byron_epoch_length
            && crypto::ed25519::verify(_hdr.signature(), _hdr.delegate_vkey(), _hdr.signed_data());
    }

    bool block::body_hash_ok() const
    {
        return _proof_actual == _hdr.proof();
    }

    void block::foreach_update_proposal(const std::function<void(const param_update_proposal &)> &observer) const
    {
        for (const auto &proposal: _body.updates.proposals)
            observer(proposal);
    }
}
