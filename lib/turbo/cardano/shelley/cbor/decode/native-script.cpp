/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/shelley/block.hpp>

namespace turbo::cardano::shelley {
    namespace {
        void parse_script(cbor::zero2::value &script)
        {
            auto &it = script.array();
            const auto type = it.read().uint();
            switch (type) {
                case 0:
                    static_cast<void>(key_hash { it.read().bytes() });
                    break;
                case 1:
                case 2: {
                    auto &scripts = it.read().array();
                    while (!scripts.done())
                        parse_script(scripts.read());
                    break;
                }
                case 3: {
                    static_cast<void>(numeric_cast<int32_t>(it.read().int64()));
                    auto &scripts = it.read().array();
                    while (!scripts.done())
                        parse_script(scripts.read());
                    break;
                }
                default:
                    throw error(fmt::format("unsupported native script type {}", type));
            }
            if (!it.done()) [[unlikely]]
                throw error(fmt::format("native script type {} has unexpected trailing elements", type));
        }
    }

    native_script_t native_script_t::from_cbor(cbor::zero2::value &script)
    {
        parse_script(script);
        return {};
    }
}
