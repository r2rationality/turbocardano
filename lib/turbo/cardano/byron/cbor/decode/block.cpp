/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/byron/block.hpp>

namespace turbo::cardano::byron {
    boundary_block::boundary_block(const uint64_t era, const uint64_t offset, const uint64_t hdr_offset, cbor::zero2::value &block, const cardano::config &cfg):
        boundary_block { era, offset, hdr_offset, block.array(), block, cfg }
    {
    }

    boundary_block::boundary_block(const uint64_t era, const uint64_t offset, const uint64_t hdr_offset, cbor::zero2::array_reader &it, cbor::zero2::value &block, const cardano::config &cfg):
        block_base { offset, hdr_offset },
        _hdr { era, it.read(), cfg },
        _txs {},
        _raw { block.data_raw() }
    {
    }

    block::tx_list block::tx_list::from_cbor(const block &blk, const uint8_t *block_begin, cbor::zero2::value &v)
    {
        std::vector<tx> decoded {};
        if (!v.indefinite()) [[likely]]
            decoded.reserve(v.special_uint());
        auto &it = v.array();
        size_t i = 0;
        while (!it.done()) {
            auto &tx_it = it.read().array();
            auto &tx_val = tx_it.read();
            auto &tx_ref = decoded.emplace_back(blk, tx_val.data_begin() - block_begin, tx_val, i++);
            tx_ref.parse_witnesses(tx_it.read());
        }
        return { std::move(decoded) };
    }

    block::ssc_payload_t block::ssc_payload_t::from_cbor(cbor::zero2::value &v)
    {
        return { v.data_raw() };
    }

    block::dlg_payload_t block::dlg_payload_t::from_cbor(cbor::zero2::value &v)
    {
        return { v.data_raw() };
    }

    block::upd_payload_t block::upd_payload_t::from_cbor(const block &blk, cbor::zero2::value &v)
    {
        upd_payload_t res {};
        auto &it = v.array();
        {
            auto &p_it = it.read().array();
            while (!p_it.done()) {
                auto &r_prop = p_it.read();
                auto &prop_it = r_prop.array();
                param_update upd { .protocol_ver=protocol_version::from_cbor(prop_it.read()) };
                {
                    auto &bvermod_it = prop_it.read().array();
                    {
                        auto &vx_it = bvermod_it.skip(2).read().array();
                        if (!vx_it.done())
                            upd.max_block_body_size = numeric_cast<uint32_t>(vx_it.read().uint());
                    }
                    {
                        auto &vx_it = bvermod_it.read().array();
                        if (!vx_it.done())
                            upd.max_block_header_size = numeric_cast<uint32_t>(vx_it.read().uint());
                    }
                    {
                        auto &vx_it = bvermod_it.read().array();
                        if (!vx_it.done())
                            upd.max_transaction_size = numeric_cast<uint32_t>(vx_it.read().uint());
                    }
                }
                param_update_proposal prop { .key_id=blk.issuer_hash(), .update=std::move(upd) };
                prop.update.hash_from_cbor(r_prop.data_raw());
                res.proposals.emplace_back(std::move(prop));
            }
        }
        {
            auto &v_it = it.read().array();
            while (!v_it.done()) {
                auto &vote = v_it.read();
                if (vote.type_byte() == 0x84) {
                    auto &vote_it = vote.array();
                    // Function argument evaluation order is unspecified, so decode these first.
                    const auto vkey = vote_it.read().bytes();
                    const auto proposal_id = vote_it.read().bytes();
                    const auto vote_yes = vote_it.read().special() == cbor::special_val::s_true;
                    const auto sig = vote_it.read().bytes();
                    res.votes.emplace_back(
                        crypto::blake2b::digest<key_hash>(vkey.subbuf(0, 32)),
                        proposal_id,
                        vote_yes,
                        sig
                    );
                }
            }
        }
        res.raw = v.data_raw();
        return res;
    }

    block::body_t block::body_t::from_cbor(const block &blk, const uint8_t *block_begin, cbor::zero2::value &v)
    {
        auto &it = v.array();
        return {
            tx_list::from_cbor(blk, block_begin, it.read()),
            ssc_payload_t::from_cbor(it.read()),
            dlg_payload_t::from_cbor(it.read()),
            upd_payload_t::from_cbor(blk, it.read())
        };
    }

    block::block(const uint64_t era, const uint64_t offset, const uint64_t hdr_offset, cbor::zero2::value &blk, const cardano::config &cfg):
        block { era, offset, hdr_offset, blk.array(), blk, cfg }
    {
    }

    block::block(const uint64_t era, const uint64_t offset, const uint64_t hdr_offset, cbor::zero2::array_reader &it, cbor::zero2::value &blk, const cardano::config &cfg):
        block_base { offset, hdr_offset },
        _hdr { era, it.read(), cfg },
        _body { body_t::from_cbor(*this, blk.data_begin(), it.read()) },
        _proof_actual { compute_proof_data(_body.txs.txs_view, _body.dlgs.raw, _body.updates.raw) },
        _raw { blk.data_raw() }
    {
    }
}
