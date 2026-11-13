/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/block.hpp>

namespace turbo::cardano::dijkstra {
    const block_hash &block_header::hash() const
    {
        if (!_hash)
            _hash.emplace(crypto::blake2b::digest<block_hash>(_raw));
        return *_hash;
    }

    const block_hash &block_header::prev_hash() const { return _prev_hash ? *_prev_hash : config().byron_genesis_hash; }
    uint64_t block_header::height() const { return _block_number; }
    uint64_t block_header::slot() const { return _slot; }
    buffer block_header::issuer_vkey() const { return _issuer_vkey; }
    protocol_version block_header::protocol_ver() const
    {
        return _protocol_version;
    }
    const header_version_info_t &block_header::version_info() const { return _version_info; }
    const cardano::vrf_vkey &block_header::vrf_vkey() const { return _vrf_vkey; }
    const vrf_cert &block_header::nonce_vrf() const { return _nonce_vrf; }
    const vrf_cert &block_header::leader_vrf() const { return _nonce_vrf; }
    uint32_t block_header::body_size() const { return _body_size; }
    const block_hash &block_header::body_hash() const { return _body_hash; }
    const operational_cert &block_header::op_cert() const { return _op_cert; }
    buffer block_header::signature() const { return _signature; }
    buffer block_header::raw() const { return _raw; }
    buffer block_header::body_raw() const { return _body_raw; }
    const buffer &block_header::data_raw() const { return _raw; }
    bool block_header::body_contains_leios_certificate() const { return _contains_leios_certificate; }
    const std::optional<eb_announcement_t> &block_header::eb_announcement() const { return _eb_announcement; }

    uint32_t block::body_size() const { return numeric_cast<uint32_t>(_body_raw.size()); }
    const block_header &block::header() const { return _header; }
    const block_hash &block::body_hash() const
    {
        if (!_body_hash)
            _body_hash.emplace(crypto::blake2b::digest<block_hash>(_body_raw));
        return *_body_hash;
    }
    const tx_list &block::txs() const { return _transactions->txs_view; }
    const invalid_tx_set &block::invalid_txs() const { return _invalid_transactions; }
    const std::optional<leios_certificate_t> &block::leios_certificate() const { return _leios_certificate; }
    const std::optional<uint8_vector> &block::peras_certificate() const { return _peras_certificate; }
}
