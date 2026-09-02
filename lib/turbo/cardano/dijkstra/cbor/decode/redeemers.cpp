/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/transaction.hpp>
#include <turbo/plutus/types.hpp>

namespace turbo::cardano::dijkstra {
    namespace {
        redeemer_tag redeemer_tag_from_cbor(cbor::zero2::value &v)
        {
            const auto raw = v.uint();
            if (raw > 6) [[unlikely]]
                throw error(fmt::format("unsupported Dijkstra redeemer tag: {}", raw));
            return static_cast<redeemer_tag>(raw);
        }
    }

    redeemer_t redeemer_t::from_cbor(cbor::zero2::map_reader &it)
    {
        auto &key = it.read_key();
        auto &key_it = key.array();
        const auto tag = redeemer_tag_from_cbor(key_it.read());
        const auto ref_idx = numeric_cast<uint32_t>(key_it.read().uint());
        if (!key_it.done()) [[unlikely]]
            throw error("unexpected trailing Dijkstra redeemer key elements");

        auto &value = it.read_val(std::move(key));
        auto &value_it = value.array();
        auto &data = value_it.read();
        plutus::data::validate_cbor(data);
        const auto data_raw = data.data_raw();
        const auto budget = ex_units::from_cbor(value_it.read());
        if (!value_it.done()) [[unlikely]]
            throw error("unexpected trailing Dijkstra redeemer value elements");
        return {{ tag, ref_idx, uint8_vector { data_raw.begin(), data_raw.end() }, budget }};
    }

    redeemers_t redeemers_t::from_cbor(cbor::zero2::value &v)
    {
        if (v.type() != cbor::major_type::map) [[unlikely]]
            throw error("Dijkstra redeemers must use the map encoding");
        redeemers_t res {};
        if (!v.indefinite()) [[likely]]
            res.items.reserve(v.special_uint());
        auto &it = v.map();
        while (!it.done()) {
            auto redeemer = redeemer_t::from_cbor(it).value;
            const auto before = res.items.size();
            res.add(std::move(redeemer));
            if (res.items.size() == before) [[unlikely]]
                throw error("duplicate Dijkstra redeemer pointer");
        }
        if (res.items.empty()) [[unlikely]]
            throw error("Dijkstra redeemers must be nonempty when supplied");
        res.raw = v.data_raw();
        return res;
    }
}
