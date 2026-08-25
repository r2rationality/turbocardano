/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/shelley/block.hpp>

namespace turbo::cardano::shelley {
    namespace {
        template<typename T>
        void decode_witnesses(transaction_witness_set_t &res, cbor::zero2::value &v)
        {
            if (!v.indefinite()) [[likely]]
                res.items.reserve(res.items.size() + v.special_uint());
            auto &it = v.array();
            while (!it.done())
                res.items.emplace_back(T::from_cbor(it.read()));
        }

        void decode_scripts(transaction_witness_set_t &res, cbor::zero2::value &v)
        {
            if (!v.indefinite()) [[likely]]
                res.items.reserve(res.items.size() + v.special_uint());
            auto &it = v.array();
            while (!it.done())
                res.items.emplace_back(
                    std::in_place_type<script_info>,
                    script_type::native,
                    it.read().data_raw());
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
                    throw error(fmt::format("duplicate shelley::transaction_witness_set element type: {}", type));
                seen_types |= mask;
            }
            auto &val = it.read_val(std::move(key));
            switch (type) {
                case 0: decode_witnesses<tx_wit_shelley_vkey>(res, val); break;
                case 1: decode_scripts(res, val); break;
                case 2: decode_witnesses<tx_wit_shelley_bootstrap>(res, val); break;
                default: throw error(fmt::format("unsupported shelley::transaction_witness_set element type: {}", type));
            }
        }
        res.raw = v.data_raw();
        return res;
    }

}
