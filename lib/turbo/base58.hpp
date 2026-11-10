#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */
#include <turbo/common/bytes.hpp>
#include <turbo/base-encoding.hpp>

namespace turbo::base58 {
    inline uint8_vector decode(const std::string_view text)
    {
        static constexpr auto codes = base_encoding::make_code_set("123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz");
        const auto first = text.find_first_not_of('1');
        const auto zeros = first == text.npos ? text.size() : first;
        uint8_vector out{};
        out.reserve(text.size());
        for (const auto c: text.substr(zeros)) {
            const auto value = base_encoding::decode_digit(c, codes);
            if (value < 0) [[unlikely]]
                throw error{"invalid base58 character: {}", c};
            auto carry = static_cast<unsigned>(value);
            for (auto &digit: out) {
                carry += 58U * digit;
                digit = static_cast<uint8_t>(carry);
                carry >>= 8;
            }
            while (carry) {
                out.emplace_back(static_cast<uint8_t>(carry));
                carry >>= 8;
            }
        }
        out.resize(out.size() + zeros, 0);
        std::reverse(out.begin(), out.end());
        return out;
    }
}
