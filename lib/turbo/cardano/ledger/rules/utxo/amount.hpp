#pragma once
/* Copyright (c) 2026 R2 Rationality OÜ. License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */
#include <turbo/cardano/common/types.hpp>
#include <turbo/math/big-int.hpp>

namespace turbo::cardano::ledger::rules {
    // A transaction has fewer than 2^64 encoded quantities, each at most 2^64-1.
    // Two limbs retain exact aggregate values without allocating on each addition.
    struct amount_sum {
        uint64_t low = 0;
        uint64_t high = 0;

        amount_sum(uint64_t value=0): low { value } {}
        bool operator==(const amount_sum &) const =default;

        amount_sum &operator+=(const amount_sum &other)
        {
            const auto before = low;
            low += other.low;
            high += other.high + (low < before);
            return *this;
        }

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.low, self.high);
        }

        cpp_int integer() const { return (cpp_int { high } << 64) + low; }
    };
    using asset_sums = map_t<script_hash, map_t<asset_name_t, amount_sum>>;
}

namespace fmt {
    template<> struct formatter<turbo::cardano::ledger::rules::amount_sum>: formatter<std::string> {
        auto format(const auto &value, format_context &ctx) const
        {
            return formatter<std::string>::format(value.integer().template convert_to<std::string>(), ctx);
        }
    };
}
