/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/transaction.hpp>

namespace turbo::cardano::dijkstra {
    namespace {
        std::optional<decoded_cbor_t<auxiliary_data_t>> auxiliary_data_from_cbor(
            cbor::zero2::array_reader &it, const bool mempool)
        {
            std::optional<decoded_cbor_t<auxiliary_data_t>> res {};
            auto &third = it.read();
            cbor::zero2::value *auxiliary_data = &third;
            if (third.type() == cbor::major_type::simple && !third.is_null()) {
                if (!mempool) [[unlikely]]
                    throw error{"Dijkstra block transaction validity must follow auxiliary data"};
                if (!third.boolean()) [[unlikely]]
                    throw error{"Dijkstra is_valid compatibility flag must be true"};
                auxiliary_data = &it.read();
            }
            if (!auxiliary_data->is_null())
                res.emplace(decoded_cbor_t<auxiliary_data_t>::from_cbor(*auxiliary_data));
            else
                static_cast<void>(auxiliary_data->special());
            return res;
        }
    }

    transaction_t transaction_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        transaction_t res {
            decoded_cbor_t<transaction_body_t>::from_cbor(it.read()),
            decoded_cbor_t<transaction_witness_set_t>::from_cbor(it.read()),
            auxiliary_data_from_cbor(it, true)
        };
        if (!it.done()) [[unlikely]]
            throw error{"unexpected trailing Dijkstra mempool transaction elements"};
        return res;
    }

    sub_transaction_t sub_transaction_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        sub_transaction_t res {
            decoded_cbor_t<sub_transaction_body_t>::from_cbor(it.read()),
            decoded_cbor_t<transaction_witness_set_t>::from_cbor(it.read())
        };
        auto &auxiliary_data = it.read();
        if (!auxiliary_data.is_null())
            res.auxiliary_data.emplace(decoded_cbor_t<auxiliary_data_t>::from_cbor(auxiliary_data));
        else
            static_cast<void>(auxiliary_data.special());
        if (!it.done()) [[unlikely]]
            throw error{"unexpected trailing Dijkstra sub-transaction elements"};
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
            const size_t idx):
        tx_base { blk, blk_off, idx, false }
    {
        auto &it = v.array();
        _body = transaction_body_t::from_cbor(it.read());
        parse_witnesses(it.read());
        // Runtime transactions only need the validated auxiliary data's encoding.
        const auto auxiliary_data = auxiliary_data_from_cbor(it, false);
        if (auxiliary_data)
            _auxiliary_data_raw = auxiliary_data->original_cbor();
        _invalid = !it.read().boolean();
        if (!it.done()) [[unlikely]]
            throw error{"unexpected trailing Dijkstra transaction elements"};
        _raw = v.data_raw();
    }
}
