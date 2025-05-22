#pragma once
/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include <limits>
#include "format.hpp"
#include "error.hpp"

namespace daedalus_turbo {
    template<typename TO, typename FROM>
    constexpr TO numeric_cast(const FROM from)
    {
        if constexpr (std::numeric_limits<FROM>::is_signed == std::numeric_limits<TO>::is_signed) {
          if (from > std::numeric_limits<TO>::max()) [[unlikely]]
            throw error(fmt::format("can't convert {} {} to {}: the value is larger than {}",
                typeid(FROM).name(), from, typeid(TO).name(), std::numeric_limits<TO>::max()));
          if (from < std::numeric_limits<TO>::min()) [[unlikely]]
              throw error(fmt::format("can't convert {} {} to {}: the value is too small", typeid(FROM).name(), from, typeid(TO).name()));
          return static_cast<TO>(from);
        }
        if constexpr (std::numeric_limits<FROM>::is_signed) {
            if (from < 0) [[unlikely]]
                throw error(fmt::format("can't convert {} {} to {}: the value is native", typeid(FROM).name(), from, typeid(TO).name()));
            if (std::numeric_limits<FROM>::max() > std::numeric_limits<TO>::max()
                    && from > static_cast<FROM>(std::numeric_limits<TO>::max())) [[unlikely]]
                throw error(fmt::format("can't convert {} {} to {}: the value is too big", typeid(FROM).name(), from, typeid(TO).name()));
            return static_cast<TO>(from);
        }
        if constexpr (std::numeric_limits<FROM>::digits > std::numeric_limits<TO>::digits) {
            if (from > static_cast<FROM>(std::numeric_limits<TO>::max())) [[unlikely]]
                throw error(fmt::format("can't convert {} {} to {}: the value is too big", typeid(FROM).name(), from, typeid(TO).name()));
        } else {
            if (static_cast<TO>(from) > std::numeric_limits<TO>::max()) [[unlikely]]
                throw error(fmt::format("can't convert {} {} to {}: the value is too big", typeid(FROM).name(), from, typeid(TO).name()));
        }
        return static_cast<TO>(from);
    }
}
