/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/babbage/block.hpp>

namespace turbo::cardano::babbage {
    script_t script_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        const auto type = it.read().uint();
        script_type decoded_type {};
        switch (type) {
            case 0: decoded_type = script_type::native; break;
            case 1: decoded_type = script_type::plutus_v1; break;
            case 2: decoded_type = script_type::plutus_v2; break;
            default: throw error(fmt::format("unsupported script_type: {}", type));
        }
        auto &script = it.read();
        script_t res { script_info {
            decoded_type,
            decoded_type == script_type::native ? script.data_raw() : script.bytes()
        } };
        if (!it.done()) [[unlikely]]
            throw error("unexpected trailing script elements");
        return res;
    }
}
