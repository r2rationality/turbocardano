/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/conway/transaction.hpp>
#include <turbo/plutus/costs-config.hpp>

namespace turbo::cardano::conway {
    namespace {
        const std::vector<std::string> &cost_model_names(const uint64_t language)
        {
            static const std::vector<std::string> empty {};
            switch (language) {
                case 0: return plutus::costs::cost_arg_names_v1();
                case 1: return plutus::costs::cost_arg_names_v2();
                case 2: return plutus::costs::cost_arg_names_v3();
                default: return empty;
            }
        }

        plutus_cost_model cost_model_from_cbor(const uint64_t language, cbor::zero2::value &v)
        {
            plutus_cost_model::raw_value_type values {};
            if (!v.indefinite()) [[likely]]
                values.reserve(v.special_uint());
            auto &it = v.array();
            while (!it.done()) {
                auto &value = it.read();
                switch (value.type()) {
                    case cbor::major_type::uint: values.emplace_back(numeric_cast<int64_t>(value.uint())); break;
                    case cbor::major_type::nint: values.emplace_back(-numeric_cast<int64_t>(value.nint())); break;
                    default: throw error(fmt::format("unsupported cost model value type: {}", value.type()));
                }
            }
            return { std::move(values), cost_model_names(language) };
        }
    }

    cost_models_t cost_models_t::from_cbor(cbor::zero2::value &v)
    {
        cost_models_t res {};
        if (!v.indefinite()) [[likely]]
            res.value.items.reserve(v.special_uint());
        auto &it = v.map();
        while (!it.done()) {
            auto &key_v = it.read_key();
            const auto key = key_v.uint();
            if (key > 255) [[unlikely]]
                throw error(fmt::format("unsupported Conway language: {}", key));
            auto &value_v = it.read_val(std::move(key_v));
            res.value.items.emplace_hint(res.value.items.end(), key, cost_model_from_cbor(key, value_v));
        }
        return res;
    }
}
