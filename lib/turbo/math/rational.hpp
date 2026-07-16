#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cbor/fwd.hpp>
#include <turbo/common/format.hpp>
#include <turbo/json-fwd.hpp>

namespace turbo {
    using cpp_rational_storage = byte_array<64>;

    struct rational_u64 {
        uint64_t numerator = 0;
        uint64_t denominator = 1;

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.numerator, self.denominator);
        }

        static rational_u64 from_cbor(cbor::zero2::array_reader &);
        static rational_u64 from_cbor(cbor::zero2::value &);
        static rational_u64 from_json(const json::value &);
        static rational_u64 from_double(double);
        void to_cbor(cbor::encoder &) const;

        bool operator==(const auto &b) const
        {
            return numerator == b.numerator && denominator == b.denominator;
        }

        operator double() const
        {
            return static_cast<double>(numerator) / denominator;
        }

        void normalize();
    };
}

namespace fmt {
    template<>
    struct formatter<turbo::rational_u64>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{} % {}", v.numerator, v.denominator);
        }
    };
}