#pragma once
/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include <limits>
#include "format.hpp"
#include "error.hpp"

namespace turbo {
    template<typename T>
    consteval std::string_view type_name()
    {
#   if defined(_MSC_VER)
        constexpr std::string_view fn = __FUNCSIG__;
        constexpr auto start = fn.find("type_name<") + 10;
        constexpr auto end = fn.rfind(">(");
#   else
        constexpr std::string_view fn = __PRETTY_FUNCTION__;
        constexpr auto start = fn.find("T = ") + 4;
        constexpr auto end = fn.rfind(']');
#   endif
        return fn.substr(start, end - start);
    }

    template<std::integral TO, std::integral FROM>
    constexpr TO numeric_cast(const FROM from)
    {
        if constexpr (std::numeric_limits<FROM>::is_signed == std::numeric_limits<TO>::is_signed) {
            if constexpr (std::numeric_limits<FROM>::max() > std::numeric_limits<TO>::max()) {
                if (from > static_cast<FROM>(std::numeric_limits<TO>::max())) [[unlikely]]
                    throw error(fmt::format("can't convert {} {} to {}: the value is larger than {}",
                        type_name<FROM>(), from, type_name<TO>(), std::numeric_limits<TO>::max()));
            }
            if constexpr (std::numeric_limits<FROM>::is_signed) {
                if (from < static_cast<FROM>(std::numeric_limits<TO>::min())) [[unlikely]]
                    throw error(fmt::format("can't convert {} {} to {}: the value is smaller than {}",
                        type_name<FROM>(), from, type_name<TO>(), std::numeric_limits<TO>::min()));
            }
        } else {
            if constexpr (std::numeric_limits<FROM>::is_signed) {
                if (from < FROM{0}) [[unlikely]]
                    throw error(fmt::format("can't convert {} {} to {}: the value is negative", type_name<FROM>(), from, type_name<TO>()));
            }
            if constexpr (std::numeric_limits<FROM>::digits > std::numeric_limits<TO>::digits) {
                if (from > static_cast<FROM>(std::numeric_limits<TO>::max())) [[unlikely]]
                    throw error(fmt::format("can't convert {} {} to {}: the value is larger than {}",
                        type_name<FROM>(), from, type_name<TO>(), std::numeric_limits<TO>::max()));
            }
        }
        return static_cast<TO>(from);
    }
}
