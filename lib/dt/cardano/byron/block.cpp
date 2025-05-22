/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/cardano/byron/block.hpp>

namespace daedalus_turbo::cardano::byron {
    static blake2b_256_hash merkle_leaf_hash(const buffer tx_raw)
    {
        uint8_vector data {};
        data << 0 << tx_raw;
        return blake2b<blake2b_256_hash>(data);
    }

    static blake2b_256_hash merkle_node_hash(const blake2b_256_hash &l, const blake2b_256_hash &r)
    {
        uint8_vector data {};
        data << 1 << l << r;
        return blake2b<blake2b_256_hash>(data);
    }

    static tx_hash make_merkle_tree_root(const tx_list &txs)
    {
        using merkle_level = vector<tx_hash>;
        using merkle_tree = vector<merkle_level>;

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

        return blake2b<tx_hash>(uint8_vector {});
    }

    proof_data_extended_t proof_data_extended_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return {
            proof_data_t {
                proof_data_t::tx_proof_t::from_cbor(it.read()),
                it.skip(1).read().bytes(),
                it.read().bytes(),
            },
            v.data_raw()
        };
    }

    block_header::byron_block_sig_t block_header::byron_block_sig_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        if (const auto typ = it.read().uint(); typ != 2) [[unlikely]]
            throw error(fmt::format("unsupported byron block signature type: {}", typ));
        return { delegate_sig_t::from_cbor(it.read()) };
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

    block_header::block_header(const uint64_t era, cbor::zero2::array_reader &it, cbor::zero2::value &hdr, const cardano::config &cfg):
        block_header_base { era, cfg },
        _protocol_magic { it.read() },
        _prev_hash { it.read().bytes() },
        _proof { decltype(_proof)::from_cbor(it.read()) },
        _consensus { decltype(_consensus)::from_cbor(it.read()) },
        _extra { it.read() },
        _hdr_raw { hdr.data_raw() },
        _hash { boundary_block_header::padded_hash(0x01, _hdr_raw) }
    {
    }

    boundary_block::boundary_block(const uint64_t era, const uint64_t offset, const uint64_t hdr_offset, cbor::zero2::array_reader &it, cbor::zero2::value &block, const cardano::config &cfg):
        block_base { offset, hdr_offset },
        _hdr { era, it.read(), cfg },
        _txs {},
        _raw { block.data_raw() }
    {
    }

    tx::input_list tx::parse_inputs(cbor::zero2::value &v)
    {
        auto &it = v.array();
        input_list res {};
        while (!it.done()) {
            auto &txi_it = it.read().array();
            if (const auto typ = txi_it.read().uint(); typ != 0) [[unlikely]]
                throw error(fmt::format("unsupported byron tx_input type: {}", typ));
            auto pv = cbor::zero2::parse(txi_it.read().tag().read().bytes());
            res.emplace_back(tx_out_ref::from_cbor(pv.get()));
        }
        return res;
    }

    tx_output_list tx::parse_outputs(cbor::zero2::value &v)
    {
        auto &it = v.array();
        tx_output_list res {};
        while (!it.done()) {
            res.emplace_back(tx_output::from_cbor(it.read()));
        }
        return res;
    }

    tx::tx(const cardano::block_base &blk, const uint64_t blk_off, cbor::zero2::value &tx, const size_t idx, const bool invalid):
        tx_base { blk, blk_off, idx, invalid }
    {
        auto &it = tx.array();
        _inputs = parse_inputs(it.read());
        _outputs = parse_outputs(it.read());
        _raw = tx.data_raw();
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
            _hash.emplace(blake2b<tx_hash>(_raw));
        return *_hash;
    }

    const input_set &tx::inputs() const
    {
        throw error("byron inputs are unordered - cannot returned an order input set - use foreach_input instead!");
    }

    void tx::foreach_input(const input_observer_t &observer) const
    {
        for (const auto &txi: _inputs)
            observer(txi);
    }

    const tx_output_list &tx::outputs() const
    {
        return _outputs;
    }

    void tx::parse_witnesses(cbor::zero2::value &v)
    {
        auto &it = v.array_sized();
        _wits.reserve(v.special_uint());
        while (!it.done()) {
            auto &wit = it.read();
            auto &w_it = wit.array();
            switch (const auto typ = w_it.read().uint(); typ) {
                case 0: _wits.emplace_back(tx_wit_byron_vkey::from_cbor(w_it.read())); break;
                case 2: _wits.emplace_back(tx_wit_byron_redeemer::from_cbor(w_it.read())); break;
                [[unlikely]] default: throw error(fmt::format("unsupported byron witness type: {}", typ));
            }
        }
        if (_wits.size() != _inputs.size()) [[unlikely]]
            throw error(fmt::format("slot: {} tx: {}: #wits: {} != #inputs: {}", _blk.slot_object(), hash(), _wits.size(), _inputs.size()));
        _wits_raw = v.data_raw();
    }

    buffer tx::raw() const
    {
        return _raw;
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
                blake2b<blake2b_256_hash>(tx_wits_enc.cbor()),
            },
            blake2b<blake2b_256_hash>(dlg_raw),
            blake2b<blake2b_256_hash>(upd_raw)
        };
    }

    block::tx_list block::tx_list::parse_txs(const block &block, const uint8_t *block_begin, cbor::zero2::value &v)
    {
        decltype(block::tx_list::txs) txs {};
        if (!v.indefinite()) [[likely]]
            txs.reserve(v.special_uint());
        auto &it = v.array();
        size_t i = 0;
        while (!it.done()) {
            auto &tx_i = it.read();
            auto &tx_it = tx_i.array();
            auto &tx_val = tx_it.read();
            auto &tx_ref = txs.emplace_back(block, tx_val.data_begin() - block_begin, tx_val, i++);
            tx_ref.parse_witnesses(tx_it.read());
        }
        return { std::move(txs) };
    }

    block::tx_list::tx_list(vector<tx> &&new_txs):
        txs { std::move(new_txs) }
    {
        txs_view.reserve(txs.size());
        for (auto &tx: txs)
            txs_view.emplace_back(&tx);
    }

    block::upd_payload_t::upd_payload_t(const block &blk, cbor::zero2::value &v)
    {
        auto &it = v.array();
        {
            auto &r_proposals = it.read();
            auto &p_it = r_proposals.array();
            while (!p_it.done()) {
                auto &r_prop = p_it.read();
                auto &prop_it = r_prop.array();
                param_update upd { .protocol_ver=protocol_version::from_cbor(prop_it.read()) };
                {
                    auto &bvermod = prop_it.read();
                    auto &bvermod_it = bvermod.array();
                    {
                        auto &vx = bvermod_it.skip(2).read();
                        auto &vx_it = vx.array();
                        if (!vx_it.done()) {
                            upd.max_block_body_size = numeric_cast<uint32_t>(vx_it.read().uint());
                        }
                    }
                    {
                        auto &vx = bvermod_it.read();
                        auto &vx_it = vx.array();
                        if (!vx_it.done()) {
                            upd.max_block_header_size = numeric_cast<uint32_t>(vx_it.read().uint());
                        }
                    }
                    {
                        auto &vx = bvermod_it.read();
                        auto &vx_it = vx.array();
                        if (!vx_it.done()) {
                            upd.max_transaction_size = numeric_cast<uint32_t>(vx_it.read().uint());
                        }
                    }
                }
                param_update_proposal prop { .key_id=blk.issuer_hash(), .update=std::move(upd) };
                prop.update.hash_from_cbor(r_prop.data_raw());
                proposals.emplace_back(std::move(prop));
            }
        }
        {
            auto &votes_raw = it.read();
            auto &v_it = votes_raw.array();
            while (!v_it.done()) {
                auto &vote = v_it.read();
                if (vote.type_byte() == 0x84) {
                    auto &vote_it = vote.array();
                    // The order of the evaluation of function arguments is not guaranteed so pre-evaluate them!
                    const auto vkey = vote_it.read().bytes();
                    const auto proposal_id = vote_it.read().bytes();
                    const auto vote_yes = vote_it.read().special() == cbor::special_val::s_true;
                    const auto sig = vote_it.read().bytes();
                    votes.emplace_back(
                        blake2b<key_hash>(vkey.subbuf(0, 32)),
                        proposal_id,
                        vote_yes,
                        sig
                    );
                }
            }
        }
        raw = v.data_raw();
    }

    block::body_t block::body_t::from_cbor(const block &blk, const uint8_t *block_begin, cbor::zero2::value &v)
    {
        auto &it = v.array();
        return {
            { tx_list::parse_txs(blk, block_begin, it.read()) },
            { it.read() },
            { it.read() },
            { blk, it.read() }
        };
    }

    block::block(const uint64_t era, const uint64_t offset, const uint64_t hdr_offset, cbor::zero2::value &blk, const cardano::config &cfg):
        block { era, offset, hdr_offset, blk.array(), blk, cfg }
    {
    }

    block::block(const uint64_t era, const uint64_t offset, const uint64_t hdr_offset, cbor::zero2::array_reader &it, cbor::zero2::value &blk, const cardano::config &cfg):
        block_base { offset, hdr_offset },
        _hdr { era, it.read(), cfg },
        _body { decltype(_body)::from_cbor(*this, blk.data_begin(), it.read()) },
        _proof_actual { compute_proof_data(_body.txs.txs_view, _body.dlgs.raw, _body.updates.raw) },
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
        return ed25519::verify(_hdr.signature(), _hdr.delegate_vkey(), _hdr.signed_data());
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