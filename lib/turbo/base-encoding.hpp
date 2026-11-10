#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */
#include <array>
#include <string_view>

namespace turbo::base_encoding {
    using code_set = std::array<signed char, 128>;

    consteval code_set make_code_set(const std::string_view alphabet)
    {
        code_set codes{};
        codes.fill(-1);
        if (alphabet.size() > codes.size()) [[unlikely]]
            throw "encoding alphabet is too large";
        for (std::size_t i = 0; i < alphabet.size(); ++i) {
            const auto c = static_cast<unsigned char>(alphabet[i]);
            if (c >= codes.size() || codes[c] != -1)
                throw "encoding alphabet must contain distinct ASCII characters";
            codes[c] = static_cast<signed char>(i);
        }
        return codes;
    }

    constexpr signed char decode_digit(const char c, const code_set &codes) noexcept
    {
        const auto index = static_cast<unsigned char>(c);
        return index < codes.size() ? codes[index] : -1;
    }
}
