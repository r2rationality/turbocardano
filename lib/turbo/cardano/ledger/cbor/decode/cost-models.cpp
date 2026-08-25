/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/plutus/costs-config.hpp>
#include <turbo/cardano/common/types.hpp>
#include <turbo/cbor/zero2.hpp>

namespace turbo::cardano {
    using namespace crypto;

    plutus_cost_model plutus_cost_model::from_cbor(const std::vector<std::string> &names, cbor::zero2::value &v)
    {
        plutus_cost_model::raw_value_type raw_values {};
        if (!v.indefinite())
            raw_values.reserve(v.special_uint());
        auto &it = v.array();
        while (!it.done()) {
            auto &val = it.read();
            switch (const auto typ = val.type(); typ) {
                case cbor::major_type::uint: raw_values.emplace_back(numeric_cast<int64_t>(val.uint())); break;
                case cbor::major_type::nint: raw_values.emplace_back(-numeric_cast<int64_t>(val.nint())); break;
                default: throw error(fmt::format("unsupported plutus_cost_model value type: {}", typ));
            }
        }
        return plutus_cost_model { std::move(raw_values), names };
    }

    static const std::vector<std::string> &plutus_cost_model_arg_names(const uint64_t id)
    {
        static const std::vector<std::string> empty {};
        switch (id) {
            case 0: return plutus::costs::cost_arg_names_v1();
            case 1: return plutus::costs::cost_arg_names_v2();
            case 2: return plutus::costs::cost_arg_names_v3();
            default: return empty;
        }
    }

    plutus_cost_models plutus_cost_models::from_cbor(cbor::zero2::value &v)
    {
        plutus_cost_models res {};
        if (!v.indefinite())
            res.items.reserve(v.special_uint());
        for (auto &it = v.map(); !it.done(); ) {
            auto &key_v = it.read_key();
            const auto typ = key_v.uint();
            auto &val_v = it.read_val(std::move(key_v));
            res.items.emplace(typ, plutus_cost_model::from_cbor(plutus_cost_model_arg_names(typ), val_v));
        }
        return res;
    }
}
