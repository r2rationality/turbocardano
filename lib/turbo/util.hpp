#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <source_location>
#include <span>
#include <turbo/common/bytes.hpp>
#include <turbo/common/error.hpp>
#include <vector>

namespace turbo {
    struct buffer_readable: buffer {
        using buffer::buffer;

        buffer_readable(const uint8_vector &bytes):
            buffer_readable { static_cast<buffer>(bytes) }
        {
        }
    };

    static_assert(std::is_constructible_v<buffer_readable, buffer>);
    static_assert(std::is_constructible_v<buffer_readable, uint8_vector>);

    inline void span_memcpy_off(const std::span<uint8_t> &dst, size_t dst_off, const buffer &src, const std::source_location &loc=std::source_location::current())
    {
        if (dst_off >= dst.size())
            throw error(fmt::format("dst_off must be less than {} but got {} in file {} at line {}!",
                dst.size(), dst_off, loc.file_name(), loc.line()));
        if (dst.size() - dst_off < src.size())
            throw error(fmt::format("expected dst must have more than {} bytes after offset {} but got {} in file {}, line {}!",
                src.size(), dst_off, dst.size() - dst_off, loc.file_name(), loc.line()));
        if (!src.empty())
            memcpy(dst.data() + dst_off, src.data(), src.size());
    }

    inline void span_memcpy(const std::span<uint8_t> &dst, const buffer &src, const std::source_location &loc=std::source_location::current())
    {
        if (dst.size() != src.size())
            throw error(fmt::format("expected src span to be of {} bytes but got {} in file {}, line {}!",
                dst.size(), src.size(), loc.file_name(), loc.line()));
        if (!dst.empty())
            memcpy(dst.data(), src.data(), dst.size());
    }

    template <size_t SZ>
    void span_memcpy(const std::span<uint8_t> &dst, const std::span<const uint8_t, SZ> &src, const std::source_location &loc=std::source_location::current())
    {
        if (dst.size() != src.size())
            throw error(fmt::format("expected src span to be of {} bytes but got {} in file {}, line {}!",
                dst.size(), src.size(), loc.file_name(), loc.line()));
        if (!dst.empty())
            memcpy(dst.data(), src.data(), dst.size());
    }

    inline uint8_vector uint8_vector_copy(const std::span<const uint8_t> &src)
    {
        uint8_vector buf;
        buf.resize(src.size());
        if (!buf.empty())
            memcpy(buf.data(), src.data(), buf.size());
        return buf;
    }

    template <size_t SZ>
    int span_memcmp(const std::span<const uint8_t> &dst, const std::span<const uint8_t, SZ> &src, const std::source_location &loc=std::source_location::current())
    {
        if (dst.size() != src.size()) [[unlikely]]
            throw error(fmt::format("expected src span to be of {} bytes but got {} in file {}, line {}!",
                dst.size(), src.size(), loc.file_name(), loc.line()));
        return dst.empty() ? 0 : memcmp(dst.data(), src.data(), dst.size());
    }

    template<class ForwardIt, class T, class Compare>
    ForwardIt binary_search(ForwardIt first, ForwardIt last, const T& value, Compare cmp)
    {
        ForwardIt i = std::lower_bound(first, last, value, cmp);
        if (i != last && !cmp(value, *i))
            return i;
        return last;
    }

    inline std::string to_lower(const std::string &s)
    {
        std::string res {};
        std::transform(s.begin(), s.end(), std::back_inserter(res), ::tolower);
        return res;
    }
}

namespace fmt {
    template<>
    struct formatter<turbo::buffer_readable>: formatter<turbo::buffer> {
        template<typename FormatContext>
        auto format(const turbo::buffer_readable &bytes, FormatContext &ctx) const -> decltype(ctx.out()) {
            bool readable = true;
            for (const uint8_t *p = bytes.data(), *end = bytes.data() + bytes.size(); p < end; ++p) {
                if (*p < 0x20 || *p > 0x7F) {
                    readable = false;
                    break;
                }
            }
            if (readable)
                return fmt::format_to(ctx.out(), "'{}'", std::string_view { reinterpret_cast<const char *>(bytes.data()), bytes.size() });
            return fmt::format_to(ctx.out(), "{}", static_cast<turbo::buffer>(bytes));
        }
    };
}
