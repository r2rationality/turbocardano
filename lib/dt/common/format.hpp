/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */
#ifndef DAEDALUS_TURBO_COMMON_FORMAT_HPP
#define DAEDALUS_TURBO_COMMON_FORMAT_HPP

#include <chrono>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <source_location>
#include <span>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifndef _MSC_VER
#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Wpragmas"
#   ifndef __clang__
#       pragma GCC diagnostic ignored "-Wdangling-reference"
#       pragma GCC diagnostic ignored "-Warray-bounds"
#       pragma GCC diagnostic ignored "-Wstringop-overflow"
#   endif
#endif
#include <fmt/core.h>
#ifndef _MSC_VER
#   pragma GCC diagnostic pop
#endif

#include "error.hpp"
//#include "array.hpp"

namespace daedalus_turbo {
    using fmt::format;

    struct fmt_error: error {
        using error::error;

        template<typename ...Args>
        explicit fmt_error(const std::source_location &loc, const char *fmt, Args&&... a):
            error { fmt::format("{} in {}", fmt::format(fmt::runtime(fmt), std::forward<Args>(a)...), loc) }
        {
        }
    };
}

namespace fmt {
    template <typename T>
    concept derived_from_base = requires(T v)
    {
        { static_cast<typename T::base_type>(v) };
    };

    template<>
    struct formatter<std::span<const uint8_t>>: formatter<int> {
        template<typename FormatContext>
        auto format(const std::span<const uint8_t> &data, FormatContext &ctx) const -> decltype(ctx.out()) {
            auto out_it = ctx.out();
            for (uint8_t v: data) {
                out_it = fmt::format_to(out_it, "{:02X}", v);
            }
            return out_it;
        }
    };

    template<size_t SZ>
    struct formatter<std::array<const uint8_t, SZ>>: formatter<std::span<const uint8_t>> {
    };

    template<size_t SZ>
    struct formatter<std::array<uint8_t, SZ>>: formatter<std::span<const uint8_t>> {
    };

    template<size_t SZ>
    struct formatter<char[SZ]>: formatter<int> {
        template<typename FormatContext>
        auto format(const char v[SZ], FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", std::string_view { v, SZ });
        }
    };

    template<typename X, typename Y>
    struct formatter<std::pair<X, Y>>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "({}, {})", v.first, v.second);
        }
    };

    template<typename T, typename A>
    struct formatter<std::vector<T, A>>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            auto out_it = fmt::format_to(ctx.out(), "[");
            for (auto it = v.begin(); it != v.end(); ++it) {
                const std::string sep { std::next(it) == v.end() ? "" : ", " };
                out_it = fmt::format_to(out_it, "{}{}", *it, sep);
            }
            return fmt::format_to(out_it, "]");
        }
    };

    template<typename T, typename A>
    struct formatter<std::list<T, A>>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            auto out_it = fmt::format_to(ctx.out(), "[");
            for (auto it = v.begin(); it != v.end(); ++it) {
                const std::string sep { std::next(it) == v.end() ? "" : ", " };
                out_it = fmt::format_to(out_it, "{}{}", *it, sep);
            }
            return fmt::format_to(out_it, "]");
        }
    };

    template<typename T, typename A>
    struct formatter<std::set<T, std::less<T>, A>>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            auto out_it = fmt::format_to(ctx.out(), "[");
            for (auto it = v.begin(); it != v.end(); ++it) {
                const std::string sep { std::next(it) == v.end() ? "" : ", " };
                out_it = fmt::format_to(out_it, "{}{}", *it, sep);
            }
            return fmt::format_to(out_it, "]");
        }
    };

    template<typename K, typename V, typename A>
    struct formatter<std::map<K, V, std::less<K>, A>>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            auto out_it = fmt::format_to(ctx.out(), "{{");
            for (auto it = v.begin(); it != v.end(); ++it) {
                const std::string sep { std::next(it) == v.end() ? "" : ", " };
                out_it = fmt::format_to(out_it, "{}={}{}", it->first, it->second, sep);
            }
            return fmt::format_to(out_it, "}}");
        }
    };

    template<typename T>
    struct formatter<std::optional<T>>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            if (v)
                return fmt::format_to(ctx.out(), "{}", *v);
            return fmt::format_to(ctx.out(), "std::nullopt");
        }
    };

    template<typename T>
    struct formatter<std::unique_ptr<T>>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            if (v)
                return fmt::format_to(ctx.out(), "{}", *v);
            return fmt::format_to(ctx.out(), "nullptr");
        }
    };

    template<typename T>
    struct formatter<std::shared_ptr<T>>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            if (v)
                return fmt::format_to(ctx.out(), "{}", *v);
            return fmt::format_to(ctx.out(), "nullptr");
        }
    };

    template<>
    struct formatter<std::thread::id>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            std::stringstream ss {};
            ss << v;
            return fmt::format_to(ctx.out(), "{}", ss.str());
        }
    };

    template<>
    struct formatter<std::chrono::time_point<std::chrono::system_clock>>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out())
        {
            std::ostringstream ss {};
            ss << v;
            return fmt::format_to(ctx.out(), "{}", ss.str());
        }
    };

    template<>
    struct formatter<std::chrono::seconds>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out())
        {
            std::ostringstream ss {};
            ss << v;
            return fmt::format_to(ctx.out(), "{}", ss.str());
        }
    };

    template<>
    struct formatter<std::source_location>: formatter<int> {
        template<typename FormatContext>
        auto format(const std::source_location &v, FormatContext &ctx) const -> decltype(ctx.out())
        {
            return fmt::format_to(ctx.out(), "{}:{} `{}`", v.file_name(), v.line(), v.function_name());
        }
    };

    template<derived_from_base T>
    struct formatter<T>: formatter<int> {
        template<typename FormatContext>
        auto format(const typename T::base_type &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", v);
        }
    };
}

#endif // !DAEDALUS_TURBO_COMMON_FORMAT_HPP