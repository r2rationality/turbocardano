/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/transaction.hpp>
#include <turbo/cardano/common/cbor/decode/script.hpp>

namespace turbo::cardano::dijkstra {
    script_t script_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        const auto raw_type = it.read().uint();
        if (raw_type > 4) [[unlikely]]
            throw error(fmt::format("unsupported Dijkstra script type: {}", raw_type));
        const auto type = static_cast<script_type>(raw_type);
        auto &script = it.read();
        script_t res { ::turbo::cardano::detail::script_info_from_cbor(
            type, script, native_script_t::validate_cbor) };
        if (!it.done()) [[unlikely]]
            throw error("unexpected trailing Dijkstra script elements");
        return res;
    }
}
