/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/shelley/block.hpp>

namespace turbo::cardano::shelley {
    tx::tx(const cardano::block_base &blk, const uint64_t blk_off, cbor::zero2::value &tx_raw, const size_t idx, const bool invalid):
        tx_base { blk, blk_off, idx, invalid },
        _body { transaction_body_t::from_cbor(tx_raw) }
    {
    }

    void tx_base::parse_witnesses(cbor::zero2::value &v)
    {
        auto decoded = transaction_witness_set_t::from_cbor(v);
        _wits = std::move(decoded.items);
        _wits_raw = decoded.raw;
    }

    void tx_base::foreach_withdrawal(const withdrawal_observer_t &observer) const
    {
        for (const auto &[reward_id, coin]: withdrawals())
            observer(tx_withdrawal { address { reward_id }, amount { coin } });
    }

    void tx_base::foreach_param_update(const update_observer_t &observer) const
    {
        for (const auto &upd: updates())
            observer(upd);
    }

    const cert_list &tx::certs() const
    {
        return _body.certs;
    }

    const input_set &tx::inputs() const
    {
        return _body.inputs;
    }

    const tx_output_list &tx::outputs() const
    {
        return _body.outputs;
    }

    uint64_t tx::fee() const
    {
        return _body.fee;
    }

    const tx_hash &tx::hash() const
    {
        if (!_body.hash)
            _body.hash.emplace(crypto::blake2b::digest<tx_hash>(_body.raw));
        return *_body.hash;
    }

    buffer tx::raw() const
    {
        return _body.raw;
    }

    const param_update_proposal_list &tx::updates() const
    {
        return _body.updates;
    }

    std::optional<uint64_t> tx::validity_end() const
    {
        return _body.validity_end;
    }

    const withdrawal_map &tx::withdrawals() const
    {
        return _body.withdrawals;
    }

    block_hash block_base::compute_body_hash(const buffer &txs_raw, const buffer &wits_raw, const buffer &meta_raw)
    {
        const std::array<block_hash, 3> part_hashes {
            crypto::blake2b::digest<block_hash>(txs_raw),
            crypto::blake2b::digest<block_hash>(wits_raw),
            crypto::blake2b::digest<block_hash>(meta_raw)
        };
        return crypto::blake2b::digest<cardano::block_hash>(buffer { reinterpret_cast<const uint8_t *>(part_hashes.data()), sizeof(part_hashes) });
    }

    const block_kes_signature block_base::kes() const
    {
        const auto &hdr = dynamic_cast<const shelley::block_header_base &>(header());
        return {
            hdr.op_cert().hot_key,
            hdr.op_cert().sig,
            hdr.issuer_vkey(),
            hdr.signature(),
            hdr.body_raw(),
            hdr.op_cert().seq_no,
            hdr.op_cert().period,
            hdr.slot(),
            config().shelley_slots_per_kes_period,
            config().shelley_max_kes_evolutions
        };
    }

    const block_vrf block_base::vrf() const
    {
        const auto &hdr = dynamic_cast<const shelley::block_header_base &>(header());
        return block_vrf {
            hdr.vrf_vkey(),
            hdr.leader_vrf().result,
            hdr.leader_vrf().proof,
            hdr.nonce_vrf().result,
            hdr.nonce_vrf().proof,
        };
    }

    bool block_base::body_hash_ok() const
    {
        const auto &hdr = dynamic_cast<const shelley::block_header_base &>(header());
        return hdr.body_hash() == body_hash();
    }

    bool block_base::signature_ok() const
    {
        return kes().verify();
    }

    void block_base::foreach_update_proposal(const std::function<void(const param_update_proposal &)> &observer) const
    {
        foreach_tx([&](const auto &tx) {
            tx.foreach_param_update(observer);
        });
    }

    uint32_t block::body_size() const
    {
        return numeric_cast<uint32_t>(_raw.size());
    }

    const block_header_base &block::header() const
    {
        return _hdr;
    }

    const block_hash &block::body_hash() const
    {
        if (!_body_hash)
            _body_hash.emplace(compute_body_hash(_txs.raw, _txs.wits_raw, _meta.raw));
        return *_body_hash;
    }

    const tx_list &block::txs() const
    {
        return _txs.txs_view;
    }
}
