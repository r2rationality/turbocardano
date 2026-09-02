/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/conway/transaction.hpp>
#include <turbo/plutus/types.hpp>

namespace turbo::cardano::conway {
    namespace {
        redeemer_tag redeemer_tag_from_cbor(cbor::zero2::value &v)
        {
            switch (const auto typ = v.uint(); typ) {
                case 0: return redeemer_tag::spend;
                case 1: return redeemer_tag::mint;
                case 2: return redeemer_tag::cert;
                case 3: return redeemer_tag::reward;
                case 4: return redeemer_tag::vote;
                case 5: return redeemer_tag::propose;
                [[unlikely]] default: throw error(fmt::format("unsupported redeemer tag: {}", typ));
            }
        }
    }

    redeemer_t redeemer_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        const auto tag = redeemer_tag_from_cbor(it.read());
        const auto ref_idx = numeric_cast<uint32_t>(it.read().uint());
        auto &data_value = it.read();
        plutus::data::validate_cbor(data_value);
        const auto data = data_value.data_raw();
        const auto budget = ex_units::from_cbor(it.read());
        if (!it.done()) [[unlikely]]
            throw error("unexpected trailing Conway redeemer elements");
        return {{ tag, ref_idx, data, budget }};
    }

    redeemer_t redeemer_t::from_cbor(cbor::zero2::map_reader &it)
    {
        auto &key = it.read_key();
        auto &key_it = key.array();
        const auto tag = redeemer_tag_from_cbor(key_it.read());
        const auto ref_idx = numeric_cast<uint32_t>(key_it.read().uint());
        if (!key_it.done()) [[unlikely]]
            throw error("unexpected trailing Conway redeemer key elements");
        auto &value = it.read_val(std::move(key));
        auto &value_it = value.array();
        auto &data_value = value_it.read();
        plutus::data::validate_cbor(data_value);
        const auto data = data_value.data_raw();
        const auto budget = ex_units::from_cbor(value_it.read());
        if (!value_it.done()) [[unlikely]]
            throw error("unexpected trailing Conway redeemer value elements");
        return {{ tag, ref_idx, data, budget }};
    }
}
