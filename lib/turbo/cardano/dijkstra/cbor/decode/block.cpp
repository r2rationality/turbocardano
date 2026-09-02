/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/block.hpp>

namespace turbo::cardano::dijkstra {
    namespace {
        invalid_tx_set invalid_transactions_from_cbor(cbor::zero2::value &v)
        {
            auto *items = &v;
            if (v.type() == cbor::major_type::tag) {
                auto &tag = v.tag();
                if (tag.id() != 258) [[unlikely]]
                    throw error(fmt::format("expected invalid transaction set tag 258 but got: {}", tag.id()));
                items = &tag.read();
            }
            invalid_tx_set result {};
            if (!items->indefinite())
                result.reserve(items->special_uint());
            auto &it = items->array();
            while (!it.done()) {
                const auto before = result.size();
                result.emplace_hint(result.end(), numeric_cast<uint16_t>(it.read().uint()));
                if (result.size() == before) [[unlikely]]
                    throw error("duplicate Dijkstra invalid transaction index");
            }
            if (result.empty()) [[unlikely]]
                throw error("Dijkstra invalid transaction set must be nonempty when supplied");
            result.raw = v.data_raw();
            return result;
        }
    }

    leios_certificate_t leios_certificate_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        leios_certificate_t res {
            it.read().bytes(),
            it.read().bytes()
        };
        if (!it.done()) [[unlikely]]
            throw error("unexpected trailing Leios certificate elements");
        return res;
    }

    block_transactions_t block_transactions_t::from_cbor(
            const block_base &blk, const uint8_t *block_begin, cbor::zero2::value &v)
    {
        std::vector<tx> decoded {};
        if (!v.indefinite())
            decoded.reserve(v.special_uint());
        auto &it = v.array();
        while (!it.done()) {
            auto &raw = it.read();
            decoded.emplace_back(blk, raw.data_begin() - block_begin, raw, decoded.size());
        }
        return { std::move(decoded), v.data_raw(), {} };
    }

    block::block(const uint64_t era, const uint64_t offset, const uint64_t hdr_offset,
            cbor::zero2::value &v, const cardano::config &cfg):
        block { era, offset, hdr_offset, v.array(), v, cfg }
    {
    }

    block::block(const uint64_t era, const uint64_t offset, const uint64_t hdr_offset,
            cbor::zero2::array_reader &it, cbor::zero2::value &v, const cardano::config &cfg):
        block_base { offset, hdr_offset },
        _header { era, it.read(), cfg }
    {
        auto &body = it.read();
        auto &body_it = body.array();

        auto &invalid = body_it.read();
        if (!invalid.is_null()) {
            _invalid_transactions = invalid_transactions_from_cbor(invalid);
        } else {
            static_cast<void>(invalid.special());
            _invalid_transactions.raw = invalid.data_raw();
        }

        auto &transactions = body_it.read();
        _transactions = std::make_unique<block_transactions_t>(
            block_transactions_t::from_cbor(*this, v.data_begin(), transactions));

        auto &leios = body_it.read();
        if (!leios.is_null())
            _leios_certificate.emplace(leios_certificate_t::from_cbor(leios));
        else
            static_cast<void>(leios.special());

        auto &peras = body_it.read();
        if (!peras.is_null()) {
            const auto bytes = peras.bytes();
            _peras_certificate.emplace(bytes.begin(), bytes.end());
        } else {
            static_cast<void>(peras.special());
        }

        if (!body_it.done()) [[unlikely]]
            throw error("unexpected trailing Dijkstra block body elements");
        _body_raw = body.data_raw();
        if (!it.done()) [[unlikely]]
            throw error("unexpected trailing Dijkstra block elements");
        _raw = v.data_raw();

        if (_header.body_contains_leios_certificate() != _leios_certificate.has_value()) [[unlikely]]
            throw error("Dijkstra header/body Leios certificate presence mismatch");
        for (const auto tx_idx: _invalid_transactions) {
            if (tx_idx >= _transactions->txs.size()) [[unlikely]]
                throw error(fmt::format("Dijkstra invalid transaction index {} is out of range", tx_idx));
            mark_invalid_tx(tx_idx);
        }
    }
}
