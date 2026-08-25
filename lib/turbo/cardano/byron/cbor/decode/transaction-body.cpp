/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/byron/block.hpp>

namespace turbo::cardano::byron {
    transaction_inputs_t transaction_inputs_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        transaction_inputs_t res {};
        while (!it.done()) {
            auto &txi_it = it.read().array();
            if (const auto typ = txi_it.read().uint(); typ != 0) [[unlikely]]
                throw error(fmt::format("unsupported byron tx_input type: {}", typ));
            auto &tag = txi_it.read().tag();
            if (tag.id() != 24) [[unlikely]]
                throw error(fmt::format("expected a tag with id 24 but got: {}", tag.id()));
            const auto ref_data = tag.read().bytes();
            auto parsed = cbor::zero2::parse(ref_data);
            auto &ref = parsed.get();
            auto &ref_it = ref.array();
            const auto tx_id = ref_it.read().bytes();
            const auto output_idx = ref_it.read().uint();
            res.emplace_back(tx_id, output_idx);
            if (!ref_it.done() || ref.data_raw().size() != ref_data.size()) [[unlikely]]
                throw error("unexpected trailing Byron transaction_input data");
        }
        return res;
    }

    transaction_output_t transaction_output_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        auto &address_it = it.read().array();
        auto &tag = address_it.read().tag();
        if (tag.id() != 24) [[unlikely]]
            throw error(fmt::format("expected a tag with id 24 but got: {}", tag.id()));
        auto address = tag.read().bytes();
        static_cast<void>(address_it.read().uint());
        if (!address_it.done()) [[unlikely]]
            throw error("unexpected trailing Byron address data");
        transaction_output_t res {{ address, it.read().uint() }};
        if (!it.done()) [[unlikely]]
            throw error("unexpected trailing Byron transaction_output data");
        return res;
    }

    transaction_outputs_t transaction_outputs_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        transaction_outputs_t res {};
        if (!v.indefinite()) [[likely]]
            res.reserve(v.special_uint());
        while (!it.done())
            res.emplace_back(std::move(transaction_output_t::from_cbor(it.read()).value));
        return res;
    }

    transaction_body_t transaction_body_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return {
            transaction_inputs_t::from_cbor(it.read()),
            transaction_outputs_t::from_cbor(it.read()),
            v.data_raw()
        };
    }

}
