/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/alonzo/block.hpp>
#include <turbo/plutus/context.hpp>
#include <turbo/plutus/machine.hpp>

namespace turbo::cardano::alonzo {
    tx::tx(const cardano::block_base &blk, const uint64_t blk_off, cbor::zero2::value &tx_raw, const size_t idx, const bool invalid):
        tx_base { blk, blk_off, idx, invalid },
        _body { transaction_body_t::from_cbor(tx_raw) }
    {
    }

    void tx_base::parse_witnesses(cbor::zero2::value &v)
    {
        auto decoded = transaction_witness_set_t::from_cbor(v);
        _wits = std::move(decoded.items);
        _redeemers = std::move(decoded.redeemers.items);
        _redeemers_raw = decoded.redeemers.raw;
        _wits_raw = decoded.raw;
    }

    const tx_redeemer_map *tx_base::redeemer_items() const
    {
        return &_redeemers;
    }

    buffer tx_base::redeemer_bytes() const
    {
        return _redeemers_raw;
    }

    using namespace turbo::plutus;


    void tx_base::foreach_collateral(const input_observer_t &observer) const
    {
        for (const auto &txi: collateral_inputs())
            observer(txi);
    }

    void tx_base::foreach_required_signer(const signer_observer_t &observer) const
    {
        for (const auto &s: required_signers())
            observer(s);
    }


    const cert_list &tx::certs() const
    {
        return _body.certs;
    }

    const input_set &tx::collateral_inputs() const
    {
        return _body.collateral_inputs;
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

    const input_set &tx::inputs() const
    {
        return _body.inputs;
    }

    const multi_mint_map &tx::mints() const
    {
        return _body.mints;
    }

    const param_update_proposal_list &tx::updates() const
    {
        return _body.updates;
    }

    const tx_output_list &tx::outputs() const
    {
        return _body.outputs;
    }

    buffer tx::raw() const
    {
        return _body.raw;
    }

    const signer_set &tx::required_signers() const
    {
        return _body.required_signers;
    }

    std::optional<uint64_t> tx::validity_start() const
    {
        return _body.validity_start;
    }

    std::optional<uint64_t> tx::validity_end() const
    {
        return _body.validity_end;
    }

    const withdrawal_map &tx::withdrawals() const
    {
        return _body.withdrawals;
    }

    block_hash block_base::compute_body_hash(const buffer &txs_raw, const buffer &wits_raw, const buffer &meta_raw, const buffer &invalid_raw)
    {
        const std::array<block_hash, 4> part_hashes {
            crypto::blake2b::digest<block_hash>(txs_raw),
            crypto::blake2b::digest<block_hash>(wits_raw),
            crypto::blake2b::digest<block_hash>(meta_raw),
            crypto::blake2b::digest<block_hash>(invalid_raw),
        };
        return crypto::blake2b::digest<block_hash>(buffer { reinterpret_cast<const uint8_t *>(part_hashes.data()), sizeof(part_hashes) });
    }

    uint32_t block::body_size() const
    {
        return numeric_cast<uint32_t>(_raw.size());
    }

    const cardano::block_header_base &block::header() const
    {
        return _hdr;
    }

    const block_hash &block::body_hash() const
    {
        if (!_body_hash)
            _body_hash.emplace(compute_body_hash(_txs.raw, _txs.wits_raw, _meta.raw, _invalid_txs.raw));
        return *_body_hash;
    }

    const tx_list &block::txs() const
    {
        return _txs.txs_view;
    }

    const invalid_tx_set &block::invalid_txs() const
    {
        return _invalid_txs;
    }
}
