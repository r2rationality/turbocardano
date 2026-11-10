#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/util.hpp>
#include <turbo/base-encoding.hpp>

namespace turbo::base64 {
	using code_set = base_encoding::code_set;

	inline uint8_vector decode_explicit(const std::string_view &in, const code_set &codes)
	{
		uint8_vector out {};
        int val = 0, valb = -8;
        for (size_t pos = 0, sz = in.size(); pos < sz; ++pos) {
            signed char c = in[pos];
            if (c == '=')
                break;
            const auto digit = base_encoding::decode_digit(c, codes);
            if (digit < 0) [[unlikely]]
                throw error{"unsupported encoding character: '0x{:x}' at pos {} in {}!", c, pos, in};
            val = (val << 6) + digit;
            valb += 6;
            if (valb >= 0) {
                out.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
                valb -= 8;
            }
        }
        return out;
	}

	inline uint8_vector decode(const std::string_view &in)
    {
        static constexpr auto codes = base_encoding::make_code_set("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/");
		return decode_explicit(in, codes);
    }

    inline uint8_vector decode_url(const std::string_view &in)
    {
        static constexpr auto codes = base_encoding::make_code_set("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_");
		return decode_explicit(in, codes);
    }
}
