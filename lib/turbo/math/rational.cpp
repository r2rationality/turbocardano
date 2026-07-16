/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <numeric>
#include <turbo/cbor/encoder.hpp>
#include <turbo/cbor/zero2.hpp>
#include <turbo/json.hpp>
#include "rational.hpp"

namespace turbo {
    void rational_u64::normalize()
    {
        if (denominator == 1)
            return;
        if (numerator == 0) {
            if (denominator != 0)
                denominator = 1;
            return;
        }
        if (numerator == denominator) {
            numerator = 1;
            denominator = 1;
            return;
        }
        const auto div = std::gcd(numerator, denominator);
        if (div > 1) {
            numerator /= div;
            denominator /= div;
        }
    }

    rational_u64 rational_u64::from_double(double d)
    {
        double num = d;
        uint64_t denominator = 1;
        while (std::fabs(num - static_cast<uint64_t>(num)) > 1e-6) {
            if (denominator >= 1'000'000'000)
                throw error(fmt::format("an unsupported value for a conversion to rational: {}", d));
            num *= 10;
            denominator *= 10;
        }
        rational_u64 res { static_cast<uint64_t>(num), denominator };
        res.normalize();
        return res;
    }

    rational_u64 rational_u64::from_cbor(cbor::zero2::array_reader &it)
    {
        const auto num = it.read().uint();
        const auto denom = it.read().uint();
        rational_u64 res { num, denom };
        res.normalize();
        return res;
    }

    rational_u64 rational_u64::from_cbor(cbor::zero2::value &v)
    {
        if (v.type() == cbor::major_type::tag)
            return from_cbor(v.tag().read().array());
        return from_cbor(v.array());
    }

    rational_u64 rational_u64::from_json(const json::value &v)
    {
        if (v.is_object())
            return { json::value_to<uint64_t>(v.at("numerator")), json::value_to<uint64_t>(v.at("denominator")) };
        return from_double(json::value_to<double>(v));
    }

    void rational_u64::to_cbor(cbor::encoder &enc) const
    {
        enc.tag(30);
        enc.array(2)
            .uint(numerator)
            .uint(denominator);
    }
}
