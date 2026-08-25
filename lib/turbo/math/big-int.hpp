#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#define BOOST_DETAIL_EMPTY_VALUE_BASE
#include <boost/multiprecision/cpp_int.hpp>
#include <turbo/cbor/encoder.hpp>
#include <turbo/cbor/zero2.hpp>
#include <turbo/common/bytes.hpp>
#include <turbo/common/format.hpp>
#include <turbo/math/rational.hpp>

namespace turbo {
    using boost::multiprecision::cpp_int;
    using boost::multiprecision::cpp_rational;
    using boost::multiprecision::numerator;
    using boost::multiprecision::denominator;

    static constexpr size_t big_int_max_size = 8192;

    inline const cpp_rational &rational_from_storage(const cpp_rational_storage &x)
    {
        static_assert(sizeof(cpp_rational) == sizeof(cpp_rational_storage));
        static_assert(alignof(cpp_rational) <= alignof(cpp_rational_storage));
        return *reinterpret_cast<const cpp_rational*>(x.data());
    }

    inline cpp_rational &rational_from_storage(cpp_rational_storage &x)
    {
        return const_cast<cpp_rational&>(rational_from_storage(const_cast<const cpp_rational_storage &>(x)));
    }

    inline cpp_rational rational_from_r64(const rational_u64 &v)
    {
        return { v.numerator, v.denominator };
    }

    inline cpp_int big_uint_from_bytes(const buffer data)
    {
        if (data.size() > big_int_max_size)
            throw error(fmt::format("big ints larger than {} bytes are not supported but got: {}!", big_int_max_size, data.size()));
        cpp_int val = 0;
        for (const uint8_t &b: data) {
            val *= 256;
            val += b;
        }
        return val;
    }

    inline cpp_int big_nint_from_bytes(const buffer data)
    {
        auto val = big_uint_from_bytes(data);
        ++val;
        val *= -1;
        return val;
    }

    inline cpp_int big_uint_from_cbor(cbor::zero2::value &v)
    {
        if (!v.indefinite()) [[likely]]
            return big_uint_from_bytes(v.bytes());
        thread_local uint8_vector bytes {};
        v.to_bytes(bytes);
        return big_uint_from_bytes(bytes);
    }

    inline cpp_int big_nint_from_cbor(cbor::zero2::value &v)
    {
        if (!v.indefinite()) [[likely]]
            return big_nint_from_bytes(v.bytes());
        thread_local uint8_vector bytes {};
        v.to_bytes(bytes);
        return big_nint_from_bytes(bytes);
    }

    inline cpp_int big_int_from_cbor(cbor::zero2::value &v)
    {
        using cbor::major_type;
        switch (v.type()) {
            case major_type::uint: return { v.uint() };
            case major_type::nint: return (cpp_int { v.nint_raw() } + 1) * -1;
            case major_type::tag: {
                auto &t = v.tag();
                switch (const auto id = t.id(); id) {
                    case 2: return big_uint_from_cbor(t.read());
                    case 3: return big_nint_from_cbor(t.read());
                    default: throw error(fmt::format("unsupported tag type for a bigint: {}!", id));
                }
            }
            default: throw error(fmt::format("cannot interpret cbor value as a bigint: {}", v.to_string()));
        }
    }

    inline void _raw_big_uint_to_cbor(cbor::encoder &enc, const cpp_int &val)
    {
        thread_local uint8_vector buf(0x1000);
        buf.clear();
        auto val_copy = val;
        while (val_copy) {
            buf.emplace_back(val_copy & 0xFF);
            val_copy >>= 8;
        }
        enc.bytes_reverse(buf);
    }

    inline void big_int_to_cbor(cbor::encoder &enc, const cpp_int &val)
    {
        if (val >= 0) [[likely]] {
            if (val <= std::numeric_limits<uint64_t>::max()) {
                enc.uint(static_cast<uint64_t>(val));
                return;
            }
            enc.tag(2);
            _raw_big_uint_to_cbor(enc, val);
        } else {
            const auto val_uint = -(val + 1);
            if (val_uint <= std::numeric_limits<uint64_t>::max()) {
                enc.nint(static_cast<uint64_t>(val_uint));
                return;
            }
            enc.tag(3);
            _raw_big_uint_to_cbor(enc, val_uint);
        }
    }
}

namespace fmt {
    template<typename T>
    struct formatter<boost::multiprecision::number<T>>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            std::ostringstream ss {};
            ss << v;
            return fmt::format_to(ctx.out(), "{}", ss.str());
        }
    };

    template<>
    struct formatter<turbo::cpp_rational>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{} % {}", static_cast<uint64_t>(turbo::numerator(v)), static_cast<uint64_t>(turbo::denominator(v)));
        }
    };
}
