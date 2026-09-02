/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/allegra/block.hpp>
#include <turbo/cardano/babbage/block.hpp>
#include <turbo/cardano/common/cbor/decode/script.hpp>

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
            [[unlikely]] default: throw error(fmt::format("unsupported script_type: {}", type));
        }
        auto &script = it.read();
        script_t res { ::turbo::cardano::detail::script_info_from_cbor(
            decoded_type, script, allegra::native_script_t::validate_cbor) };
        if (!it.done()) [[unlikely]]
            throw error("unexpected trailing script elements");
        return res;
    }
}
