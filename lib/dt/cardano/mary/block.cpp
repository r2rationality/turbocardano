/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/cardano/mary/block.hpp>

namespace daedalus_turbo::cardano::mary {
    size_t tx_base::foreach_mint(const mint_observer_t &observer) const
    {
        const auto m = mints();
        for (const auto &[policy_id, p_mints]: m)
            observer(policy_id, p_mints);
        return m.size();
    }

    multi_mint_map tx_base::parse_mints(cbor::zero2::value &mints_raw)
    {
        multi_mint_map m {};
        auto &m_it = mints_raw.map();
        while (!m_it.done()) {
            auto &p_id = m_it.read_key();
            const auto policy_id_bytes = p_id.bytes();
            auto &p_mints = m_it.read_val(std::move(p_id));
            auto &p_it = p_mints.map();
            policy_mint_map p_m {};
            while (!p_it.done()) {
                auto &name_v = p_it.read_key();
                const auto name_bytes = name_v.bytes();
                auto &coin_v = p_it.read_val(std::move(name_v));
                switch (coin_v.type()) {
                    case cbor::major_type::uint: p_m.emplace_hint(p_m.end(), name_bytes, numeric_cast<int64_t>(coin_v.uint())); break;
                    case cbor::major_type::nint: p_m.emplace_hint(p_m.end(), name_bytes, -numeric_cast<int64_t>(coin_v.nint())); break;
                    [[unlikely]] default: throw error(fmt::format("expecting an int but got {}", coin_v.type()));
                }
            }
            if (!p_m.empty()) [[likely]]
                m.emplace_hint(m.end(), policy_id_bytes, std::move(p_m));
        }
        return m;
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
                default: throw error(fmt::format("unsupported tx element type: {}", typ));
            }
        }
        _raw = tx_raw.data_raw();
    }

    const cert_list &tx::certs() const
    {
        return _certs;
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

    const param_update_proposal_list &tx::updates() const
    {
        return _updates;
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

    block::block(const uint64_t era, const uint64_t offset, const uint64_t hdr_offset, cbor::zero2::value &blk, const cardano::config &cfg):
        block { era, offset, hdr_offset, blk.array(), blk, cfg }
    {
    }

    block::block(const uint64_t era, const uint64_t offset, const uint64_t hdr_offset, cbor::zero2::array_reader &it, cbor::zero2::value &blk, const cardano::config &cfg):
        block_base { offset, hdr_offset },
        _hdr { era, it.read(), cfg },
        _txs { parse_txs<tx>(*this, blk.data_begin(), it) },
        _meta { it.read() },
        _raw { blk.data_raw() }
    {
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