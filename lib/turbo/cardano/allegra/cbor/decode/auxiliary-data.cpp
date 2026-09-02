/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/allegra/auxiliary-data.hpp>
#include <turbo/cardano/allegra/block.hpp>
#include <turbo/cardano/common/cbor/decode/script.hpp>

namespace turbo::cardano::allegra {
    auxiliary_data_array_t auxiliary_data_array_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        auxiliary_data_array_t res { metadata_t::from_cbor(it.read()) };
        auto &scripts_v = it.read();
        if (!scripts_v.indefinite()) [[likely]]
            res.auxiliary_scripts.reserve(scripts_v.special_uint());
        auto &scripts = scripts_v.array();
        while (!scripts.done()) {
            auto &script = scripts.read();
            res.auxiliary_scripts.emplace_back(
                ::turbo::cardano::detail::script_info_from_cbor(
                    script_type::native, script, native_script_t::validate_cbor));
        }
        if (!it.done()) [[unlikely]]
            throw error("unexpected trailing auxiliary_data_array elements");
        return res;
    }

    auxiliary_data_t auxiliary_data_t::from_cbor(cbor::zero2::value &v)
    {
        switch (v.type()) {
            case cbor::major_type::map: return { metadata_t::from_cbor(v) };
            case cbor::major_type::array: return { auxiliary_data_array_t::from_cbor(v) };
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
