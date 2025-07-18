#pragma once
/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include <limits>
#include "format.hpp"
#include "error.hpp"

namespace turbo {
    template<std::integral TO, std::integral FROM>
    constexpr TO numeric_cast(const FROM from)
    {
        if constexpr (std::numeric_limits<FROM>::is_signed == std::numeric_limits<TO>::is_signed) {
            if constexpr (std::numeric_limits<FROM>::max() > std::numeric_limits<TO>::max()) {
                if (from > static_cast<FROM>(std::numeric_limits<TO>::max())) [[unlikely]]
                    throw error(fmt::format("can't convert {} {} to {}: the value is larger than {}",
                        typeid(FROM).name(), from, typeid(TO).name(), std::numeric_limits<TO>::max()));
                if constexpr (std::numeric_limits<FROM>::is_signed) {
                    if (from < static_cast<FROM>(std::numeric_limits<TO>::min())) [[unlikely]]
                        throw error(fmt::format("can't convert {} {} to {}: the value is smaller than {}",
                            typeid(FROM).name(), from, typeid(TO).name(), std::numeric_limits<TO>::min()));
                }
            }
        } else {
            if constexpr (std::numeric_limits<FROM>::is_signed) {
                if (from < FROM{0}) [[unlikely]]
                    throw error(fmt::format("can't convert {} {} to {}: the value is negative", typeid(FROM).name(), from, typeid(TO).name()));
            }
            if constexpr (std::numeric_limits<FROM>::digits > std::numeric_limits<TO>::digits) {
                if (from > static_cast<FROM>(std::numeric_limits<TO>::max())) [[unlikely]]
                    throw error(fmt::format("can't convert {} {} to {}: the value is larger than {}",
                        typeid(FROM).name(), from, typeid(TO).name(), std::numeric_limits<TO>::max()));
            }
        }
        return static_cast<TO>(from);
    }
}
