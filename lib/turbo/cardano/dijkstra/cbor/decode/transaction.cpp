/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/transaction.hpp>

namespace turbo::cardano::dijkstra {
    namespace {
        transaction_t transaction_from_cbor(cbor::zero2::value &v, const bool allow_legacy_validity_flag)
        {
            auto &it = v.array();
            transaction_t res {
                transaction_body_t::from_cbor(it.read()),
                transaction_witness_set_t::from_cbor(it.read())
            };
            auto &third = it.read();
            cbor::zero2::value *auxiliary_data = &third;
            if (third.type() == cbor::major_type::simple && !third.is_null()) {
                if (!allow_legacy_validity_flag) [[unlikely]]
                    throw error("Dijkstra block transactions cannot contain an is_valid flag");
                if (!third.boolean()) [[unlikely]]
                    throw error("Dijkstra is_valid compatibility flag must be true");
                auxiliary_data = &it.read();
            }
            if (!auxiliary_data->is_null())
                res.auxiliary_data.emplace(auxiliary_data_t::from_cbor(*auxiliary_data));
            else
                static_cast<void>(auxiliary_data->special());
            if (!it.done()) [[unlikely]]
                throw error("unexpected trailing Dijkstra transaction elements");
            return res;
        }
    }

    transaction_t transaction_t::from_cbor(cbor::zero2::value &v)
    {
        return transaction_from_cbor(v, true);
    }

    sub_transaction_t sub_transaction_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        sub_transaction_t res {
            sub_transaction_body_t::from_cbor(it.read()),
            transaction_witness_set_t::from_cbor(it.read())
        };
        auto &auxiliary_data = it.read();
        if (!auxiliary_data.is_null())
            res.auxiliary_data.emplace(auxiliary_data_t::from_cbor(auxiliary_data));
        else
            static_cast<void>(auxiliary_data.special());
        if (!it.done()) [[unlikely]]
            throw error("unexpected trailing Dijkstra sub-transaction elements");
        return res;
    }

    void tx_base::parse_witnesses(cbor::zero2::value &v)
    {
        auto decoded = transaction_witness_set_t::from_cbor(v);
        _wits = std::move(decoded.items);
        _redeemers = std::move(decoded.redeemers.items);
        _redeemers_raw = decoded.redeemers.raw;
        _wits_raw = decoded.raw;
    }

    tx::tx(const cardano::block_base &blk, const uint64_t blk_off, cbor::zero2::value &v,
            const size_t idx, const bool invalid, const bool mempool):
        tx_base { blk, blk_off, idx, invalid }
    {
        auto decoded = transaction_from_cbor(v, mempool);
        _body = std::move(decoded.body);
        _wits = std::move(decoded.witnesses.items);
        _redeemers = std::move(decoded.witnesses.redeemers.items);
        _redeemers_raw = decoded.witnesses.redeemers.raw;
        _wits_raw = decoded.witnesses.raw;
        _auxiliary_data = std::move(decoded.auxiliary_data);
        _raw = v.data_raw();
    }
}
