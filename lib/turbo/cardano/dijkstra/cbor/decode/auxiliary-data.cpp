/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/transaction.hpp>
#include <turbo/cardano/common/cbor/decode/script.hpp>

namespace turbo::cardano::dijkstra {
    namespace {
        void add_scripts(cbor::zero2::value &v, const script_type type, std::vector<script_info> &scripts)
        {
            if (!v.indefinite()) [[likely]]
                scripts.reserve(scripts.size() + v.special_uint());
            auto &it = v.array();
            while (!it.done()) {
                auto &script = it.read();
                scripts.emplace_back(::turbo::cardano::detail::script_info_from_cbor(
                    type, script, native_script_t::validate_cbor));
            }
        }

        auxiliary_data_array_t auxiliary_data_array_from_cbor(cbor::zero2::value &v)
        {
            auto &it = v.array();
            auxiliary_data_array_t res { metadata_t::from_cbor(it.read()) };
            auto &scripts = it.read();
            if (!scripts.indefinite())
                res.auxiliary_scripts.reserve(scripts.special_uint());
            auto &script_it = scripts.array();
            while (!script_it.done()) {
                auto &script = script_it.read();
                res.auxiliary_scripts.emplace_back(
                    ::turbo::cardano::detail::script_info_from_cbor(
                        script_type::native, script, native_script_t::validate_cbor));
            }
            if (!it.done()) [[unlikely]]
                throw error("unexpected trailing Dijkstra auxiliary_data_array elements");
            return res;
        }
    }

    auxiliary_data_map_t auxiliary_data_map_t::from_cbor(cbor::zero2::value &v)
    {
        auto &tag = v.tag();
        if (tag.id() != 259) [[unlikely]]
            throw error(fmt::format("expected auxiliary data tag 259 but got: {}", tag.id()));
        auxiliary_data_map_t res {};
        uint8_t seen = 0;
        auto &it = tag.read().map();
        while (!it.done()) {
            auto &key_v = it.read_key();
            const auto key = key_v.uint();
            if (key > 5) [[unlikely]]
                throw error(fmt::format("unsupported Dijkstra auxiliary_data_map key: {}", key));
            const auto mask = static_cast<uint8_t>(1U << key);
            if (seen & mask) [[unlikely]]
                throw error(fmt::format("duplicate Dijkstra auxiliary_data_map key: {}", key));
            seen |= mask;
            auto &value_v = it.read_val(std::move(key_v));
            switch (key) {
                case 0: res.metadata = metadata_t::from_cbor(value_v); break;
                case 1: add_scripts(value_v, script_type::native, res.native_scripts); break;
                case 2: add_scripts(value_v, script_type::plutus_v1, res.plutus_v1_scripts); break;
                case 3: add_scripts(value_v, script_type::plutus_v2, res.plutus_v2_scripts); break;
                case 4: add_scripts(value_v, script_type::plutus_v3, res.plutus_v3_scripts); break;
                case 5: add_scripts(value_v, script_type::plutus_v4, res.plutus_v4_scripts); break;
                default: std::unreachable();
            }
        }
        return res;
    }

    auxiliary_data_t auxiliary_data_t::from_cbor(cbor::zero2::value &v)
    {
        switch (v.type()) {
            case cbor::major_type::map: return { metadata_t::from_cbor(v) };
            case cbor::major_type::array: return { auxiliary_data_array_from_cbor(v) };
            case cbor::major_type::tag: return { auxiliary_data_map_t::from_cbor(v) };
            [[unlikely]] default:
                throw error(fmt::format("unsupported Dijkstra auxiliary_data value type: {}", v.type()));
        }
    }
}
