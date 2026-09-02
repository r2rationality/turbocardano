/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/allegra/block.hpp>
#include <turbo/cardano/common/cbor/decode/script.hpp>
#include <turbo/cardano/conway/transaction.hpp>

namespace turbo::cardano::conway {
    namespace {
        cbor::zero2::value &set_items(cbor::zero2::value &v)
        {
            if (v.type() == cbor::major_type::array)
                return v;
            auto &reader = v.tag();
            if (reader.id() != 258) [[unlikely]]
                throw error(fmt::format("expected a tag with id 258 but got: {}!", reader.id()));
            return reader.read();
        }

        template<typename T>
        void decode_witnesses(transaction_witness_set_t &res, cbor::zero2::value &v)
        {
            auto &items = set_items(v);
            if (!items.indefinite()) [[likely]]
                res.items.reserve(res.items.size() + items.special_uint());
            auto &it = items.array();
            if (it.done()) [[unlikely]]
                throw error("Conway witness fields must be nonempty when supplied");
            while (!it.done())
                res.items.emplace_back(T::from_cbor(it.read()));
        }

        template<script_type TYPE>
        void decode_scripts(transaction_witness_set_t &res, cbor::zero2::value &v)
        {
            auto &items = set_items(v);
            [[maybe_unused]] flat_set<script_hash> seen {};
            if (!items.indefinite()) [[likely]] {
                const auto size = items.special_uint();
                res.items.reserve(res.items.size() + size);
                if constexpr (TYPE != script_type::native)
                    seen.reserve(size);
            }
            auto &it = items.array();
            if (it.done()) [[unlikely]]
                throw error("Conway witness fields must be nonempty when supplied");
            while (!it.done()) {
                auto &script = it.read();
                auto info = ::turbo::cardano::detail::script_info_from_cbor(
                    TYPE, script, allegra::native_script_t::validate_cbor);
                if constexpr (TYPE != script_type::native) {
                    if (!seen.emplace(info.hash()).second) [[unlikely]]
                        throw error("duplicate Conway Plutus script witness");
                }
                res.items.emplace_back(std::move(info));
            }
        }
    }

    transaction_witness_set_t transaction_witness_set_t::from_cbor(cbor::zero2::value &v)
    {
        transaction_witness_set_t res {};
        uint16_t seen_types = 0;
        auto &it = v.map();
        while (!it.done()) {
            auto &key = it.read_key();
            const auto type = key.uint();
            if (type < 16) {
                const auto mask = static_cast<uint16_t>(1U << type);
                if (seen_types & mask) [[unlikely]]
                    throw error(fmt::format("duplicate conway::transaction_witness_set element type: {}", type));
                seen_types |= mask;
            }
            auto &val = it.read_val(std::move(key));
            switch (type) {
                case 0: decode_witnesses<tx_wit_shelley_vkey>(res, val); break;
                case 1: decode_scripts<script_type::native>(res, val); break;
                case 2: decode_witnesses<tx_wit_shelley_bootstrap>(res, val); break;
                case 3: decode_scripts<script_type::plutus_v1>(res, val); break;
                case 4: decode_witnesses<tx_wit_datum>(res, val); break;
                case 5: {
                    auto redeemers = redeemers_t::from_cbor(val);
                    res.redeemers.items = std::move(redeemers.items);
                    res.redeemers.raw = redeemers.raw;
                    break;
                }
                case 6: decode_scripts<script_type::plutus_v2>(res, val); break;
                case 7: decode_scripts<script_type::plutus_v3>(res, val); break;
                [[unlikely]] default: throw error(fmt::format("unsupported conway::transaction_witness_set element type: {}", type));
            }
        }
        res.raw = v.data_raw();
        return res;
    }

}
