/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */
#ifndef DAEDALUS_TURBO_CBOR_COMPARE_HPP
#define DAEDALUS_TURBO_CBOR_COMPARE_HPP

#include <functional>
#include <dt/util.hpp>

namespace daedalus_turbo::cbor {
    struct path_t {
        void push(const size_t v)
        {
            _items.emplace_back(v);
        }

        void pop()
        {
            _items.pop_back();
        }

        std::string to_string() const
        {
            std::string res {};
            auto res_it = std::back_inserter(res);
            for (auto it = _items.begin(); it != _items.end(); ++it) {
                res_it = fmt::format_to(res_it, "{}", *it);
                if (std::next(it) != _items.end())
                    res_it = fmt::format_to(res_it, ".");
            }
            return res;
        }
    private:
        vector<size_t> _items {};
    };

    using path_formatter_t = std::function<std::string(const path_t&)>;

    struct diff_t {
        template<typename ...Args>
        diff_t(const path_t &path, const char *fmt, Args&&... a):
            _message { fmt::format("{}: {}", path.to_string(), fmt::format(fmt::runtime(fmt), std::forward<Args>(a)...) ) }
        {
        }

        const std::string &to_string() const
        {
            return _message;
        }
    private:
        std::string _message;
    };

    // ensure a custom type to specialize the formatter
    struct diff_list: vector<diff_t> {
        using vector::vector;

        void add(diff_t &&d);

        template<typename ...Args>
        void add(const path_t &path, const char *fmt, Args&&... a)
        {
            add(diff_t { path, fmt, std::forward<Args>(a)... });
        }

        operator bool() const
        {
            return empty();
        }
    };

    extern diff_list compare(const buffer &buf1, const buffer &buf2);
}

namespace fmt {
    template<>
    struct formatter<daedalus_turbo::cbor::diff_t>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", v.to_string());
        }
    };

    template<>
    struct formatter<daedalus_turbo::cbor::diff_list>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            if (v.empty())
                return fmt::format_to(ctx.out(), "same");
            auto out_it = fmt::format_to(ctx.out(), "[\n");
            for (const auto &d: v)
                out_it = fmt::format_to(out_it, "    {}\n", d.to_string());
            return fmt::format_to(out_it, "]\n");
        }
    };
}

#endif // !DAEDALUS_TURBO_CBOR_COMPARE_HPP