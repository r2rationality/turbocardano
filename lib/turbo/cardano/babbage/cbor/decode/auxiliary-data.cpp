/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/babbage/auxiliary-data.hpp>

namespace turbo::cardano::babbage {
    namespace {
        void add_scripts(cbor::zero2::value &v, const script_type type, std::vector<script_info> &scripts)
        {
            if (!v.indefinite()) [[likely]]
                scripts.reserve(scripts.size() + v.special_uint());
            auto &it = v.array();
            while (!it.done()) {
                auto &script = it.read();
                scripts.emplace_back(type, type == script_type::native ? script.data_raw() : script.bytes());
            }
        }
    }

    auxiliary_data_map_t auxiliary_data_map_t::from_cbor(cbor::zero2::value &v)
    {
        auto &tag = v.tag();
        if (tag.id() != 259) [[unlikely]]
            throw error(fmt::format("expected a tag with id 259 but got: {}", tag.id()));
        auxiliary_data_map_t res {};
        auto &it = tag.read().map();
        while (!it.done()) {
            auto &key_v = it.read_key();
            const auto key = key_v.uint();
            auto &value_v = it.read_val(std::move(key_v));
            switch (key) {
                case 0: res.metadata = metadata_t::from_cbor(value_v); break;
                case 1: add_scripts(value_v, script_type::native, res.native_scripts); break;
                case 2: add_scripts(value_v, script_type::plutus_v1, res.plutus_v1_scripts); break;
                case 3: add_scripts(value_v, script_type::plutus_v2, res.plutus_v2_scripts); break;
                default: throw error(fmt::format("unsupported auxiliary_data_map key: {}", key));
            }
        }
        return res;
    }

    auxiliary_data_t auxiliary_data_t::from_cbor(cbor::zero2::value &v)
    {
        switch (v.type()) {
            case cbor::major_type::map: return { metadata_t::from_cbor(v) };
            case cbor::major_type::array: return { auxiliary_data_array_t::from_cbor(v) };
            case cbor::major_type::tag: return { auxiliary_data_map_t::from_cbor(v) };
            [[unlikely]] default: throw error(fmt::format("unsupported auxiliary_data value type: {}", v.type()));
        }
    }

    auxiliary_data_dict_t auxiliary_data_dict_t::from_cbor(cbor::zero2::value &v)
    {
        auxiliary_data_dict_t res {};
        if (!v.indefinite()) [[likely]]
            res.reserve(v.special_uint());
        auto &it = v.map();
        while (!it.done()) {
            auto &key_v = it.read_key();
            const auto tx_idx = key_v.uint();
            auto &value_v = it.read_val(std::move(key_v));
            res.emplace_hint(res.end(), tx_idx, auxiliary_data_t::from_cbor(value_v));
        }
        res.raw = v.data_raw();
        return res;
    }
}
