/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/allegra/block.hpp>
#include <turbo/cardano/common/cbor/decode/script.hpp>
#include <turbo/cardano/conway/transaction.hpp>

namespace turbo::cardano::conway {
    namespace {
        script_type script_type_from_cbor(cbor::zero2::value &v)
        {
            switch (const auto type = v.uint(); type) {
                case 0: return script_type::native;
                case 1: return script_type::plutus_v1;
                case 2: return script_type::plutus_v2;
                case 3: return script_type::plutus_v3;
                [[unlikely]] default: throw error(fmt::format("unsupported script_type: {}", type));
            }
        }
    }

    script_t script_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        const auto s_typ = script_type_from_cbor(it.read());
        auto &script = it.read();
        script_t res { ::turbo::cardano::detail::script_info_from_cbor(
            s_typ, script, allegra::native_script_t::validate_cbor) };
        if (!it.done()) [[unlikely]]
            throw error("unexpected trailing script elements");
        return res;
    }
}
