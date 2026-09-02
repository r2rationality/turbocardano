/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/transaction.hpp>

namespace turbo::cardano::dijkstra {
    namespace {
        template<bool materialize>
        void parse_native_script(cbor::zero2::value &script, native_script_t *res)
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
                        parse_native_script<true>(child, &child_res);
                    } else {
                        parse_native_script<false>(child, nullptr);
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
                case 6: {
                    auto &credential = it.read();
                    if constexpr (materialize)
                        res->required_credential = credential_t::from_cbor(credential);
                    else
                        static_cast<void>(credential_t::from_cbor(credential));
                    break;
                }
                [[unlikely]] default:
                    throw error(fmt::format("unsupported Dijkstra native script type: {}", type));
            }
            if (!it.done()) [[unlikely]]
                throw error(fmt::format("Dijkstra native script type {} has trailing elements", type));
        }
    }

    native_script_t native_script_t::from_cbor(cbor::zero2::value &v)
    {
        native_script_t res {};
        parse_native_script<true>(v, &res);
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

    void native_script_t::validate_cbor(cbor::zero2::value &v)
    {
        parse_native_script<false>(v, nullptr);
    }
}
