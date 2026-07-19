#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/plutus/types-core.hpp>

namespace fmt {
    template<>
        struct formatter<turbo::plutus::bint_type>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::bint_type &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", *v);
        }
    };

    template<>
        struct formatter<turbo::plutus::str_type>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::str_type &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", *v);
        }
    };

    template<>
        struct formatter<turbo::plutus::bstr_type>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::bstr_type &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", static_cast<turbo::buffer>(*v));
        }
    };

    template<>
    struct formatter<turbo::plutus::builtin_tag>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::builtin_tag &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", turbo::plutus::t_builtin { v }.name());
        }
    };

    template<>
    struct formatter<turbo::plutus::bls12_381_g1_element>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::bls12_381_g1_element &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            turbo::byte_array<48> comp {};
            blst_p1_compress(reinterpret_cast<byte *>(comp.data()), &v.get());
            return fmt::format_to(ctx.out(), "0x{}", comp);
        }
    };

    template<>
    struct formatter<turbo::plutus::bls12_381_g2_element>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::bls12_381_g2_element &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            turbo::byte_array<96> comp {};
            blst_p2_compress(reinterpret_cast<byte *>(comp.data()), &v.get());
            return fmt::format_to(ctx.out(), "0x{}", comp);
        }
    };

    template<>
    struct formatter<turbo::plutus::bls12_381_ml_result>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::bls12_381_ml_result &, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "opaque");
        }
    };

    template<>
    struct formatter<turbo::plutus::data>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &vv, FormatContext &ctx) const -> decltype(ctx.out()) {
            using namespace turbo::plutus;
#ifdef NDEBUG
            return fmt::format_to(ctx.out(), "{}", vv.as_string(0));
#else
            return fmt::format_to(ctx.out(), "{}", vv.as_string(4));
#endif
        }
    };

    template<bool nested, typename OutputIt>
    OutputIt format_constant_value_to(OutputIt out, const turbo::plutus::constant::value_type &vv)
    {
        using namespace turbo;
        using namespace turbo::plutus;
        return std::visit([&out](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return fmt::format_to(out, "()");
            } else if constexpr (std::is_same_v<T, bool>) {
                return fmt::format_to(out, "{}", v ? "True" : "False");
            } else if constexpr (std::is_same_v<T, bstr_type>) {
                return fmt::format_to(out, "#{}", buffer_lowercase { static_cast<buffer>(*v) });
            } else if constexpr (std::is_same_v<T, plutus::data>) {
                if constexpr (nested)
                    return fmt::format_to(out, "{}", v);
                else
                    return fmt::format_to(out, "({})", v);
            } else if constexpr (std::is_same_v<T, str_type>) {
                return fmt::format_to(out, "\"{}\"", escape_utf8_string(*v));
            } else if constexpr (std::is_same_v<T, constant_pair>) {
                auto next = fmt::format_to(out, "(");
                next = format_constant_value_to<true>(next, *v->first);
                next = fmt::format_to(next, ", ");
                next = format_constant_value_to<true>(next, *v->second);
                return fmt::format_to(next, ")");
            } else if constexpr (std::is_same_v<T, constant_list> || std::is_same_v<T, constant_array>) {
                auto next = fmt::format_to(out, "[");
                for (auto it = v.begin(); it != v.end(); ++it) {
                    next = format_constant_value_to<true>(next, **it);
                    if (std::next(it) != v.end())
                        next = fmt::format_to(next, ", ");
                }
                return fmt::format_to(next, "]");
            } else if constexpr (std::is_same_v<T, asset_value>) {
                auto next = fmt::format_to(out, "[");
                for (auto currency_it = v->begin(); currency_it != v->end(); ++currency_it) {
                    next = fmt::format_to(next, "(#{}", buffer_lowercase {
                        buffer { currency_it->first.data(), currency_it->first.size() } });
                    next = fmt::format_to(next, ", [");
                    for (auto token_it = currency_it->second.begin(); token_it != currency_it->second.end(); ++token_it) {
                        next = fmt::format_to(next, "(#{}", buffer_lowercase {
                            buffer { token_it->first.data(), token_it->first.size() } });
                        next = fmt::format_to(next, ", {})", token_it->second);
                        if (std::next(token_it) != currency_it->second.end())
                            next = fmt::format_to(next, ", ");
                    }
                    next = fmt::format_to(next, "])");
                    if (std::next(currency_it) != v->end())
                        next = fmt::format_to(next, ", ");
                }
                return fmt::format_to(next, "]");
            } else {
                return fmt::format_to(out, "{}", v);
            }
        }, vv);
    }

    template<>
    struct formatter<turbo::plutus::constant::value_type>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &vv, FormatContext &ctx) const -> decltype(ctx.out()) {
            return format_constant_value_to<false>(ctx.out(), vv);
        }
    };

    template<>
    struct formatter<turbo::plutus::constant_type>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using namespace turbo::plutus;
            if (v->nested.empty())
                return fmt::format_to(ctx.out(), "{}", v->typ);
            if (v->typ == type_tag::list || v->typ == type_tag::array) {
                if (v->nested.size() != 1) [[unlikely]]
                    throw error(fmt::format("the nested type list for a sequence must have just one element but has {}", v->nested.size()));
                return fmt::format_to(ctx.out(), "({} {})", v->typ, v->nested.front());
            }
            if (v->typ == type_tag::pair) {
                if (v->nested.size() != 2) [[unlikely]]
                    throw error(fmt::format("the nested type list for a pair must have two elements but has {}", v->nested.size()));
                return fmt::format_to(ctx.out(), "({} {} {})", v->typ, v->nested.front(), v->nested.back());
            }
            throw error(fmt::format("unsupported constant_type: {}!", v->typ));
        }
    };

    template<>
    struct formatter<turbo::plutus::constant>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::constant &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            turbo::plutus::allocator alloc {};
            return fmt::format_to(ctx.out(), "(con {} {})", turbo::plutus::constant_type::from_val(alloc, v), *v);
        }
    };

    template<>
    struct formatter<turbo::plutus::variable>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::variable &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "v{}", v.idx);
        }
    };

    template<>
    struct formatter<turbo::plutus::t_delay>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::t_delay &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "(delay {})", v.expr);
        }
    };

    template<>
    struct formatter<turbo::plutus::apply>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::apply &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "[{} {}]", v.func, v.arg);
        }
    };

    template<>
    struct formatter<turbo::plutus::force>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::force &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "(force {})", v.expr);
        }
    };

    template<>
    struct formatter<turbo::plutus::failure>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::failure &, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "(error)");
        }
    };

    template<>
    struct formatter<turbo::plutus::t_builtin>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::t_builtin &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "(builtin {})", v.name());
        }
    };

    template<>
    struct formatter<turbo::plutus::t_constr>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::t_constr &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "(constr {} {})", v.tag, v.args);
        }
    };

    template<>
    struct formatter<turbo::plutus::t_case>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::t_case &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "(case {} {})", v.arg, v.cases);
        }
    };

    template<>
    struct formatter<turbo::plutus::term_format_ref>: formatter<int> {
        template<typename OutputIt>
        static OutputIt format_term(OutputIt out_it, const turbo::plutus::term &v, const size_t depth)
        {
            return v.visit([&](const auto &payload) {
                return format_value(out_it, payload, depth);
            });
        }

        template<typename OutputIt>
        static OutputIt format_terms(OutputIt out_it, const turbo::plutus::term_list &vals, const size_t depth)
        {
            for (auto it = vals->begin(); it != vals->end(); ++it) {
                out_it = format_term(out_it, *it, depth);
                if (std::next(it) != vals->end())
                    out_it = fmt::format_to(out_it, " ");
            }
            return out_it;
        }

        template<typename OutputIt, typename Value>
        static OutputIt format_value(OutputIt out_it, const Value &v, const size_t depth)
        {
            using namespace turbo::plutus;
            using val_type = std::decay_t<Value>;
            if constexpr (std::is_same_v<val_type, variable>) {
                const auto name_idx = v.idx < depth ? depth - 1 - v.idx : v.idx;
                return fmt::format_to(out_it, "v{}", name_idx);
            } else if constexpr (std::is_same_v<val_type, t_delay>) {
                out_it = fmt::format_to(out_it, "(delay ");
                out_it = format_term(out_it, v.expr, depth);
                return fmt::format_to(out_it, ")");
            } else if constexpr (std::is_same_v<val_type, force>) {
                out_it = fmt::format_to(out_it, "(force ");
                out_it = format_term(out_it, v.expr, depth);
                return fmt::format_to(out_it, ")");
            } else if constexpr (std::is_same_v<val_type, t_lambda>) {
                out_it = fmt::format_to(out_it, "(lam v{} ", depth);
                out_it = format_term(out_it, v.expr, depth + 1);
                return fmt::format_to(out_it, ")");
            } else if constexpr (std::is_same_v<val_type, t_builtin_spine>) {
                for (size_t i = 0; i < v.args.size(); ++i)
                    out_it = fmt::format_to(out_it, "[");
                for (size_t i = 0; i < v.forces; ++i)
                    out_it = fmt::format_to(out_it, "(force ");
                out_it = fmt::format_to(out_it, "{}", v.b);
                for (size_t i = 0; i < v.forces; ++i)
                    out_it = fmt::format_to(out_it, ")");
                for (const auto &arg: v.args) {
                    out_it = fmt::format_to(out_it, " ");
                    out_it = format_term(out_it, arg, depth);
                    out_it = fmt::format_to(out_it, "]");
                }
                return out_it;
            } else if constexpr (std::is_same_v<val_type, apply>) {
                out_it = fmt::format_to(out_it, "[");
                out_it = format_term(out_it, v.func, depth);
                out_it = fmt::format_to(out_it, " ");
                out_it = format_term(out_it, v.arg, depth);
                return fmt::format_to(out_it, "]");
            } else if constexpr (std::is_same_v<val_type, t_constr>) {
                out_it = fmt::format_to(out_it, "(constr {} ", v.tag);
                out_it = format_terms(out_it, v.args, depth);
                return fmt::format_to(out_it, ")");
            } else if constexpr (std::is_same_v<val_type, t_case>) {
                out_it = fmt::format_to(out_it, "(case ");
                out_it = format_term(out_it, v.arg, depth);
                out_it = fmt::format_to(out_it, " ");
                out_it = format_terms(out_it, v.cases, depth);
                return fmt::format_to(out_it, ")");
            } else {
                return fmt::format_to(out_it, "{}", v);
            }
        }

        template<typename FormatContext>
        auto format(const turbo::plutus::term_format_ref &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return format_term(ctx.out(), v.val, v.depth);
        }
    };

    template<>
    struct formatter<turbo::plutus::t_lambda>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::t_lambda &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "(lam v0 {})", turbo::plutus::term_format_ref { v.expr, 1 });
        }
    };

    template<>
    struct formatter<turbo::plutus::term>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::term &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", turbo::plutus::term_format_ref { v, 0 });
        }
    };

    template<>
    struct formatter<turbo::plutus::version>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::version &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", static_cast<std::string>(v));
        }
    };

    template<typename T>
    struct formatter<turbo::plutus::list_type<T>>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::list_type<T> &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", static_cast<std::pmr::vector<T>>(v));
        }
    };

    template<>
    struct formatter<turbo::plutus::value_list::value_type>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::value_list::value_type &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", static_cast<turbo::plutus::value_list::value_type::base_type>(v));
        }
    };

    template<>
    struct formatter<turbo::plutus::value_list>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::value_list &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", *v);
        }
    };

    template<>
    struct formatter<turbo::plutus::builtin_args>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::builtin_args &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            auto out_it = fmt::format_to(ctx.out(), "[");
            const auto args = v.values();
            for (size_t i = 0; i < args.size(); ++i)
                out_it = fmt::format_to(out_it, "{}{}", args[i], i + 1 < args.size() ? ", " : "");
            return fmt::format_to(out_it, "]");
        }
    };

    template<>
    struct formatter<turbo::plutus::term_list>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::term_list &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            auto out_it = ctx.out();
            for (auto it = v->begin(); it != v->end(); ++it)
                out_it = fmt::format_to(out_it, "{}{}", *it, std::next(it) != v->end() ? " " : "");
            return out_it;
        }
    };

    template<>
    struct formatter<turbo::plutus::environment::node>: formatter<int> {
        template<typename OutputIt>
        static OutputIt format_nodes(OutputIt out_it, const turbo::plutus::environment::node &v)
        {
            size_t idx = 0;
            for (auto node = &v; node; node = node->parent.get(), ++idx) {
                if (idx)
                    out_it = fmt::format_to(out_it, ", ");
                out_it = fmt::format_to(out_it, "v{}={}", idx, node->val);
            }
            return out_it;
        }

        template<typename FormatContext>
        auto format(const turbo::plutus::environment::node &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return format_nodes(ctx.out(), v);
        }
    };

    template<>
    struct formatter<turbo::plutus::environment>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::environment &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using namespace turbo::plutus;
            auto out_it = fmt::format_to(ctx.out(), "env [");
            if (const auto *node = v.get(); node)
                out_it = formatter<environment::node>::format_nodes(out_it, *node);
            return fmt::format_to(out_it, "]");
        }
    };

    template<>
    struct formatter<turbo::plutus::value>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::value &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using namespace turbo::plutus;
            return v.visit([&ctx](const auto &payload) {
                return fmt::format_to(ctx.out(), "{}", payload);
            });
        }
    };

    template<>
    struct formatter<turbo::plutus::v_builtin>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::v_builtin &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using namespace turbo::plutus;
            return fmt::format_to(ctx.out(), "(builtin {} {})", v.b.name(), v.args);
        }
    };

    template<>
    struct formatter<turbo::plutus::v_constr>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::v_constr &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using namespace turbo::plutus;
            return fmt::format_to(ctx.out(), "(constr {} {})", v.tag, v.args);
        }
    };

    template<>
    struct formatter<turbo::plutus::v_delay>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::v_delay &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using namespace turbo::plutus;
            return fmt::format_to(ctx.out(), "(delay {})", v.expr);
        }
    };

    template<>
    struct formatter<turbo::plutus::v_lambda>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::v_lambda &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using namespace turbo::plutus;
            return fmt::format_to(ctx.out(), "(lam v0 {})", term_format_ref { v.body, 1 });
        }
    };

}
