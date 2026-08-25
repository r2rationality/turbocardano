/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/babbage/block.hpp>

namespace turbo::cardano::babbage {
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

    void tx_base::foreach_referenced_input(const input_observer_t &observer) const
    {
        for (const auto &txi: ref_inputs())
            observer(txi);
    }

    const cert_list &tx::certs() const
    {
        return _body.certs;
    }

    const input_set &tx::collateral_inputs() const
    {
        return _body.collateral_inputs;
    }

    const std::optional<tx_output> &tx::collateral_return() const
    {
        return _body.collateral_return;
    }

    const std::optional<uint64_t> &tx::collateral_value() const
    {
        return _body.collateral_value;
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

    const param_update_proposal_list &tx::updates() const
    {
        return _body.updates;
    }

    const input_set &tx::ref_inputs() const
    {
        return _body.ref_inputs;
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
}
