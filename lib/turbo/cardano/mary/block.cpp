/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/mary/block.hpp>

namespace turbo::cardano::mary {
    tx::tx(const cardano::block_base &blk, const uint64_t blk_off, cbor::zero2::value &tx_raw, const size_t idx, const bool invalid):
        tx_base { blk, blk_off, idx, invalid },
        _body { transaction_body_t::from_cbor(tx_raw) }
    {
    }

    size_t tx_base::foreach_mint(const mint_observer_t &observer) const
    {
        const auto m = mints();
        for (const auto &[policy_id, p_mints]: m)
            observer(policy_id, p_mints);
        return m.size();
    }


    const cert_list &tx::certs() const
    {
        return _body.certs;
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

    const param_update_proposal_list &tx::updates() const
    {
        return _body.updates;
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
            _body_hash.emplace(compute_body_hash(_txs.raw, _txs.wits_raw, _meta.raw));
        return *_body_hash;
    }

    const tx_list &block::txs() const
    {
        return _txs.txs_view;
    }
}
