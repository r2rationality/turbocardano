/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/allegra/block.hpp>
#include <turbo/common/format.hpp>

namespace turbo::cardano::allegra {
    namespace {
        template<bool materialize>
        void parse_script(cbor::zero2::value &script, native_script_t *res)
        {
            auto &it = script.array();
            const auto type = it.read().uint();
            if constexpr (materialize)
                res->type = static_cast<native_script_t::type_t>(type);
            const auto parse_children = [&](cbor::zero2::value &children_v) {
                if constexpr (materialize) {
                    if (!children_v.indefinite()) [[likely]]
                        res->scripts.reserve(children_v.special_uint());
                }
                auto &children = children_v.array();
                while (!children.done()) {
                    auto &child = children.read();
                    if constexpr (materialize) {
                        auto &child_res = res->scripts.emplace_back();
                        parse_script<true>(child, &child_res);
                    } else {
                        parse_script<false>(child, nullptr);
                    }
                }
            };
            switch (type) {
                case 0: {
                    auto &key = it.read();
                    if constexpr (materialize)
                        res->key = key_hash { key.bytes() };
                    else
                        static_cast<void>(key_hash { key.bytes() });
                    break;
                }
                case 1:
                case 2: {
                    auto &children = it.read();
                    parse_children(children);
                    break;
                }
                case 3: {
                    auto &required = it.read();
                    if constexpr (materialize)
                        res->required = required.int64();
                    else
                        static_cast<void>(required.int64());
                    auto &children = it.read();
                    parse_children(children);
                    break;
                }
                case 4:
                case 5: {
                    auto &slot = it.read();
                    if constexpr (materialize)
                        res->slot = slot.uint();
                    else
                        static_cast<void>(slot.uint());
                    break;
                }
                [[unlikely]] default:
                    throw error(fmt::format("unsupported native script type {}", type));
            }
            if (!it.done()) [[unlikely]]
                throw error(fmt::format("native script type {} has unexpected trailing elements", type));
        }
    }

    native_script_t native_script_t::from_cbor(cbor::zero2::value &script)
    {
        native_script_t res {};
        parse_script<true>(script, &res);
        return res;
    }

    native_script_t native_script_t::from_cbor(const buffer raw)
    {
        cbor::zero2::decoder dec { raw };
        auto res = from_cbor(dec.read());
        if (!dec.done()) [[unlikely]]
            throw error("native script contains trailing CBOR data");
        return res;
    }

    void native_script_t::validate_cbor(cbor::zero2::value &script)
    {
        parse_script<false>(script, nullptr);
    }
}
