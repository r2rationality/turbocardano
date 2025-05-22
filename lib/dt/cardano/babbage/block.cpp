/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/cardano/babbage/block.hpp>

namespace daedalus_turbo::cardano::babbage {
    block_header::body_t::body_t(cbor::zero2::value &v, const cardano::config &cfg):
        body_t { v.array(), v, cfg }
    {
    }

    block_header::body_t::body_t(cbor::zero2::array_reader &it, cbor::zero2::value &v, const cardano::config &cfg):
        block_number { numeric_cast<uint32_t>(it.read().uint()) },
        slot { numeric_cast<uint32_t>(it.read().uint()) },
        prev_hash { prev_hash_from_cbor(it.read(), cfg) },
        issuer_vkey { it.read().bytes() },
        vrf_vkey { it.read().bytes() },
        nonce_vrf { vrf_cert::from_cbor(it.read()) },
        body_size { numeric_cast<uint32_t>(it.read().uint()) },
        body_hash { it.read().bytes() },
        op_cert { operational_cert::from_cbor(it.read()) },
        node_ver { protocol_version::from_cbor(it.read()) },
        raw { v.data_raw() }
    {
    }


    block_header::block_header(const uint64_t era, cbor::zero2::value &hdr, const cardano::config &cfg):
        block_header { era, hdr.array(), hdr, cfg }
    {
    }

    block_header::block_header(const uint64_t era, cbor::zero2::array_reader &it, cbor::zero2::value &hdr, const cardano::config &cfg):
        block_header_base { era, cfg },
        _body { it.read(), cfg },
        _sig { it.read().bytes() },
        _raw { hdr.data_raw() }
    {
    }

    block::block(const uint64_t era, const uint64_t offset, const uint64_t hdr_offset, cbor::zero2::value &blk, const cardano::config &cfg):
        block { era, offset, hdr_offset, blk.array(), blk, cfg }
    {
    }

    block::block(const uint64_t era, const uint64_t offset, const uint64_t hdr_offset, cbor::zero2::array_reader &it, cbor::zero2::value &blk, const cardano::config &cfg):
        block_base { offset, hdr_offset },
        _hdr { era, it.read(), cfg },
        _txs { parse_txs<tx>(*this, blk.data_begin(), it) },
        _meta { it.read() },
        _invalid_txs { it.read() },
        _raw { blk.data_raw() }
    {
        for (const auto tx_idx: _invalid_txs) {
            mark_invalid_tx(tx_idx);
        }
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

    void tx_base::parse_witnesses(cbor::zero2::value &v)
    {
        auto &it = v.map();
        while (!it.done()) {
            auto &key = it.read_key();
            const auto typ = key.uint();
            auto &val = it.read_val(std::move(key));
            switch (typ) {
                case 0: parse_witnesses_type<tx_wit_shelley_vkey>(val); break;
                case 1: parse_witnesses_script(script_type::native, val); break;
                case 2: parse_witnesses_type<tx_wit_shelley_bootstrap>(val); break;
                case 3: parse_witnesses_script(script_type::plutus_v1, val); break;
                case 4: parse_witnesses_type<tx_wit_datum>(val); break;
                case 5: parse_redeemers(val); break;
                case 6: parse_witnesses_script(script_type::plutus_v2, val); break;
                default: throw error(fmt::format("unsupported shelley::tx witness type: {}", typ));
            }
        }
        _wits_raw = v.data_raw();
    }

    tx::tx(const cardano::block_base &blk, const uint64_t blk_off, cbor::zero2::value &tx_raw, const size_t idx, const bool invalid):
        tx_base { blk, blk_off, idx, invalid }
    {
        auto &it = tx_raw.map();
        while (!it.done()) {
            auto &mk = it.read_key();
            const auto typ = mk.uint();
            auto &mv = it.read_val(std::move(mk));
            switch (typ) {
                case 0: _inputs = parse_inputs(mv); break;
                case 1: _outputs = parse_outputs(mv); break;
                case 2: _fee = mv.uint(); break;
                case 3: _validity_end.emplace(mv.uint()); break;
                case 4: _certs = parse_certs(mv); break;
                case 5: _withdrawals = parse_withdrawals(mv); break;
                case 6: _updates = parse_updates(mv); break;
                case 7: break; // metadata_hash
                case 8: _validity_start.emplace(mv.uint()); break;
                case 9: _mints = parse_mints(mv); break;
                case 11: break; // script_data_hash
                case 13: _collateral_inputs = parse_inputs(mv); break;
                case 14: _required_signers = parse_signers(mv); break;
                case 15: break; // network_id
                case 16: _collateral_return.emplace(tx_output::from_cbor(mv)); break;
                case 17: _collateral_value.emplace(mv.uint()); break;
                case 18: _ref_inputs = parse_inputs(mv); break;
                default: throw error(fmt::format("unsupported tx element type: {}", typ));
            }
        }
        _raw = tx_raw.data_raw();
    }

    const cert_list &tx::certs() const
    {
        return _certs;
    }

    const input_set &tx::collateral_inputs() const
    {
        return _collateral_inputs;
    }

    const std::optional<tx_output> &tx::collateral_return() const
    {
        return _collateral_return;
    }

    const std::optional<uint64_t> &tx::collateral_value() const
    {
        return _collateral_value;
    }

    uint64_t tx::fee() const
    {
        return _fee;
    }

    const tx_hash &tx::hash() const
    {
        if (!_hash)
            _hash.emplace(blake2b<tx_hash>(_raw));
        return *_hash;
    }

    const input_set &tx::inputs() const
    {
        return _inputs;
    }

    const multi_mint_map &tx::mints() const
    {
        return _mints;
    }

    const tx_output_list &tx::outputs() const
    {
        return _outputs;
    }

    buffer tx::raw() const
    {
        return _raw;
    }

    const signer_set &tx::required_signers() const
    {
        return _required_signers;
    }

    const param_update_proposal_list &tx::updates() const
    {
        return _updates;
    }

    const input_set &tx::ref_inputs() const
    {
        return _ref_inputs;
    }

    std::optional<uint64_t> tx::validity_start() const
    {
        return _validity_start;
    }

    std::optional<uint64_t> tx::validity_end() const
    {
        return _validity_end;
    }

    const withdrawal_map &tx::withdrawals() const
    {
        return _withdrawals;
    }
}
