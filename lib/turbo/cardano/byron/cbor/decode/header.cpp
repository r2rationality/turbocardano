/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/byron/block.hpp>

namespace turbo::cardano::byron {
    boundary_block_header::boundary_block_header(const uint64_t era, cbor::zero2::value &hdr, const cardano::config &cfg):
        boundary_block_header { era, hdr.array(), hdr, cfg }
    {
    }

    boundary_block_header::boundary_block_header(const uint64_t era, cbor::zero2::array_reader &it, cbor::zero2::value &hdr, const cardano::config &cfg):
        block_header_base { era, cfg },
        _prev_hash { it.skip(1).read().bytes() },
        _slot { it.skip(1).read().array().read().uint() * cfg.byron_epoch_length },
        _hash { padded_hash(0x00, hdr.data_raw()) },
        _hdr_raw { hdr.data_raw() }
    {
    }

    proof_data_t::tx_proof_t proof_data_t::tx_proof_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return { it.read().uint(), it.read().bytes(), it.read().bytes() };
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

    block_header::protocol_magic_t block_header::protocol_magic_t::from_cbor(cbor::zero2::value &v)
    {
        return { v.uint(), v.data_raw() };
    }

    block_header::extra_t block_header::extra_t::from_cbor(cbor::zero2::value &v)
    {
        return { v.data_raw() };
    }

    block_header::slot_id_t block_header::slot_id_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return { it.read().uint(), it.read().uint(), v.data_raw() };
    }

    block_header::byron_vkey_t block_header::byron_vkey_t::from_cbor(cbor::zero2::value &v)
    {
        return { v.bytes() };
    }

    block_header::byron_block_sig_t::delegate_sig_t block_header::byron_block_sig_t::delegate_sig_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        auto &dlg = it.read();
        auto &d_it = dlg.array();
        auto &epoch = d_it.read();
        return {
            epoch.uint(),
            epoch.data_raw(),
            byron_vkey_t::from_cbor(d_it.read()),
            byron_vkey_t::from_cbor(d_it.read()),
            d_it.read().bytes(),
            it.read().bytes()
        };
    }

    block_header::byron_block_sig_t block_header::byron_block_sig_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        if (const auto typ = it.read().uint(); typ != 2) [[unlikely]]
            throw error(fmt::format("unsupported byron block signature type: {}", typ));
        return { delegate_sig_t::from_cbor(it.read()) };
    }

    block_header::consensus_t block_header::consensus_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return {
            slot_id_t::from_cbor(it.read()),
            byron_vkey_t::from_cbor(it.read()),
            it.read().array().read().uint(),
            byron_block_sig_t::from_cbor(it.read()),
            v.data_raw()
        };
    }

    block_header::block_header(const uint64_t era, cbor::zero2::value &hdr, const cardano::config &cfg):
        block_header { era, hdr.array(), hdr, cfg }
    {
    }

    block_header::block_header(const uint64_t era, cbor::zero2::array_reader &it, cbor::zero2::value &hdr, const cardano::config &cfg):
        block_header_base { era, cfg },
        _protocol_magic { protocol_magic_t::from_cbor(it.read()) },
        _prev_hash { it.read().bytes() },
        _proof { proof_data_extended_t::from_cbor(it.read()) },
        _consensus { consensus_t::from_cbor(it.read()) },
        _extra { extra_t::from_cbor(it.read()) },
        _hdr_raw { hdr.data_raw() },
        _hash { boundary_block_header::padded_hash(0x01, _hdr_raw) }
    {
    }
}
