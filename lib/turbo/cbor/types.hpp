#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <turbo/common/format.hpp>
#include <vector>

namespace turbo::cbor {
    enum class major_type: uint8_t {
        uint = 0,
        nint = 1,
        bytes = 2,
        text = 3,
        array = 4,
        map = 5,
        tag = 6,
        simple = 7
    };

    enum class special_val: uint8_t {
        s_false = 20,
        s_true = 21,
        s_null = 22,
        s_undefined = 23,
        one_byte = 24,
        two_bytes = 25,
        four_bytes = 26,
        eight_bytes = 27,
        s_break = 31
    };

    inline std::vector<size_t> parse_value_path(const std::string_view text)
    {
        std::vector<size_t> value_path;
        std::string_view text_path { text };
        while (text_path.size() > 0) {
            size_t next_pos = text_path.find('.');
            std::string idx_text;
            if (next_pos != std::string_view::npos) {
                idx_text = text_path.substr(0, next_pos);
                text_path = text_path.substr(next_pos + 1);
            } else {
                idx_text = text_path;
                text_path = text_path.substr(0, 0);
            }
            size_t idx = std::stoull(idx_text);
            value_path.push_back(idx);
        }
        return value_path;
    }

    inline bool is_ascii(const std::span<const uint8_t> b)
    {
        for (const uint8_t *p = b.data(), *end = p + b.size(); p < end; ++p) {
            if (*p < 32 || *p > 127) [[unlikely]]
                return false;
        }
        return true;
    }
}

namespace fmt {
    template<>
    struct formatter<turbo::cbor::special_val>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using turbo::cbor::special_val;
            switch (v) {
                case special_val::s_false: return fmt::format_to(ctx.out(), "false");
                case special_val::s_true: return fmt::format_to(ctx.out(), "true");
                case special_val::s_null: return fmt::format_to(ctx.out(), "null");
                case special_val::s_undefined: return fmt::format_to(ctx.out(), "undefined");
                case special_val::one_byte: return fmt::format_to(ctx.out(), "one_byte");
                case special_val::two_bytes: return fmt::format_to(ctx.out(), "two_bytes");
                case special_val::four_bytes: return fmt::format_to(ctx.out(), "four_bytes");
                case special_val::eight_bytes: return fmt::format_to(ctx.out(), "eight_bytes");
                case special_val::s_break: return fmt::format_to(ctx.out(), "break");
                default: return fmt::format_to(ctx.out(), "special_value: {}", static_cast<int>(v));
            }
        }
    };

    template<>
    struct formatter<turbo::cbor::major_type>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using turbo::cbor::major_type;
            switch (v) {
                case major_type::uint: return fmt::format_to(ctx.out(), "uint");
                case major_type::nint: return fmt::format_to(ctx.out(), "nint");
                case major_type::bytes: return fmt::format_to(ctx.out(), "bytes");
                case major_type::text: return fmt::format_to(ctx.out(), "text");
                case major_type::array: return fmt::format_to(ctx.out(), "array");
                case major_type::map: return fmt::format_to(ctx.out(), "map");
                case major_type::tag: return fmt::format_to(ctx.out(), "tag");
                case major_type::simple: return fmt::format_to(ctx.out(), "simple");
                default: throw fmt::format_to(ctx.out(), "major_type: {}", static_cast<int>(v));
            }
        }
    };
}