/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/cardano/shelley/block.hpp>

namespace daedalus_turbo::cardano::shelley {
    param_update_proposal_list tx_base::parse_updates(cbor::zero2::value &v)
    {
        auto &it = v.array();
        flat_map<key_hash, param_update> proposals {};
        {
            auto &props = it.read();
            auto &props_it = props.map();
            while (!props_it.done()) {
                auto &k = props_it.read_key();
                const auto vk = k.bytes();
                proposals.emplace_hint(proposals.end(), vk, param_update::from_cbor(props_it.read_val(std::move(k))));
            }
        }
        const auto epoch = it.read().uint();
        param_update_proposal_list res {};
        res.reserve(proposals.size());
        for (auto &&[vk, upd]: proposals)
            res.emplace_back(vk, std::move(upd), epoch);
        return res;
    }

    withdrawal_map tx_base::parse_withdrawals(cbor::zero2::value &v)
    {
        withdrawal_map res {};
        if (!v.indefinite())
            res.reserve(v.special_uint());
        auto &it = v.map();
        while (!it.done()) {
            auto &k = it.read_key();
            auto reward_id = reward_id_t::from_cbor(k);
            res.emplace_hint(res.end(), std::move(reward_id), it.read_val(std::move(k)).uint());
        }
        return res;
    }

    input_set tx_base::parse_inputs(cbor::zero2::value &v)
    {
        input_set res {};
        set_t<tx_input>::foreach_item(
            v,
            [&](auto &iv) {
                res.emplace_hint(res.end(), tx_out_ref::from_cbor(iv));
            },
            [&](const auto sz) {
                res.reserve(sz);
            }
        );
        return res;
    }

    tx_output_list tx_base::parse_outputs(cbor::zero2::value &v)
    {
        tx_output_list res {};
        if (!v.indefinite())
            res.reserve(v.special_uint());
        auto &it = v.array();
        while (!it.done()) {
            res.emplace_back(tx_output::from_cbor(it.read()));
        }
        return res;
    }

    cert_list tx_base::parse_certs(cbor::zero2::value &v)
    {
        cert_list res {};
        set_t<tx_input>::foreach_item(
            v,
            [&](auto &iv) {
                res.emplace_back(cert_t::from_cbor(iv));
            },
            [&](const auto sz) {
                res.reserve(sz);
            }
        );
        return res;
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
                default: throw error(fmt::format("unsupported shelley::tx witness type: {}", typ));
            }
        }
        _wits_raw = v.data_raw();
    }

    tx::tx(const cardano::block_base &blk, const uint64_t blk_off, cbor::zero2::value &tx, const size_t idx, const bool invalid):
        tx_base { blk, blk_off, idx, invalid }
    {
        auto &it = tx.map();
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
                default: throw error(fmt::format("unsupported tx element type: {}", typ));
            }
        }
        _raw = tx.data_raw();
    }

    const cert_list &tx::certs() const
    {
        return _certs;
    }

    const input_set &tx::inputs() const
    {
        return _inputs;
    }

    const tx_output_list &tx::outputs() const
    {
        return _outputs;
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

    buffer tx::raw() const
    {
        return _raw;
    }

    const param_update_proposal_list &tx::updates() const
    {
        return _updates;
    }

    std::optional<uint64_t> tx::validity_end() const
    {
        return _validity_end;
    }

    const withdrawal_map &tx::withdrawals() const
    {
        return _withdrawals;
    }

    block_header::body_t::body_t(cbor::zero2::value &v, const cardano::config &cfg):
        body_t { v.array(), v, cfg }
    {
    }

    buffer block_header_base::prev_hash_from_cbor(cbor::zero2::value &v, const cardano::config &cfg)
    {
        return !v.is_null() ? v.bytes() : buffer { cfg.byron_genesis_hash };
    }

    block_header::body_t::body_t(cbor::zero2::array_reader &it, cbor::zero2::value &v, const cardano::config &cfg):
        block_number { numeric_cast<uint32_t>(it.read().uint()) },
        slot { numeric_cast<uint32_t>(it.read().uint()) },
        prev_hash { prev_hash_from_cbor(it.read(), cfg) },
        issuer_vkey { it.read().bytes() },
        vrf_vkey { it.read().bytes() },
        nonce_vrf { vrf_cert::from_cbor(it.read()) },
        leader_vrf { vrf_cert::from_cbor(it.read()) },
        body_size { numeric_cast<uint32_t>(it.read().uint()) },
        body_hash { it.read().bytes() },
        op_cert {
            it.read().bytes(),
            it.read().uint(),
            it.read().uint(),
            it.read().bytes()
        },
        node_ver { it.read().uint(), it.read().uint() },
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

    block_hash block_base::compute_body_hash(const buffer &txs_raw, const buffer &wits_raw, const buffer &meta_raw)
    {
        const std::array<block_hash, 3> part_hashes {
            blake2b<block_hash>(txs_raw),
            blake2b<block_hash>(wits_raw),
            blake2b<block_hash>(meta_raw)
        };
        return blake2b<cardano_hash_32>(buffer { reinterpret_cast<const uint8_t *>(part_hashes.data()), sizeof(part_hashes) });
    }

    const kes_signature block_base::kes() const
    {
        const auto &hdr = dynamic_cast<const shelley::block_header_base &>(header());
        return kes_signature {
            hdr.op_cert().hot_key,
            hdr.op_cert().sig,
            hdr.issuer_vkey(),
            hdr.signature(),
            hdr.body_raw(),
            hdr.op_cert().seq_no,
            hdr.op_cert().period,
            hdr.slot()
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
