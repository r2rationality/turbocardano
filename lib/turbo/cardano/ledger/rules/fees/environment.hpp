#pragma once
/* Copyright (c) 2026 R2 Rationality OÜ. License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */
#include <array>
#include <numeric>
#include <turbo/cardano/common/types.hpp>
#include <turbo/math/big-int.hpp>

namespace turbo::cardano::ledger::rules {
    // Fees.minfee: prepared from enacted parameters before workers are started.
    // No ledger state, lazy caches, or transaction-owned data are retained here.
    struct fee_environment {
        explicit fee_environment(const protocol_params &params):
            _a { params.min_fee_a }, _b { params.min_fee_b }
        {
            const auto &mem = params.ex_unit_prices.mem;
            const auto &steps = params.ex_unit_prices.steps;
            if (!mem.denominator || !steps.denominator)
                throw error("execution prices must have nonzero denominators");
            const auto gcd = std::gcd(mem.denominator, steps.denominator);
            _execution_den = cpp_int { mem.denominator / gcd } * steps.denominator;
            _mem_num = cpp_int { mem.numerator } * (steps.denominator / gcd);
            _steps_num = cpp_int { steps.numerator } * (mem.denominator / gcd);
            _prices[0] = rational_from_r64(params.min_fee_ref_script_cost_per_byte);
            for (size_t i = 1; i < _prices.size(); ++i) {
                _prefixes[i] = _prefixes[i - 1] + tier_size * _prices[i - 1];
                _prices[i] = _prices[i - 1] * cpp_rational { 6, 5 };
            }
        }

        cpp_int reference_script_fee(size_t bytes) const
        {
            const auto tier = bytes / tier_size;
            if (tier < _prices.size()) {
                const cpp_rational fee = _prefixes[tier] + (bytes % tier_size) * _prices[tier];
                return numerator(fee) / denominator(fee);
            }
            // Keep the helper exact even for an input not bounded by UTXO checks.
            auto price = _prices.back();
            auto fee = _prefixes.back();
            bytes -= (_prices.size() - 1) * tier_size;
            while (bytes >= tier_size) {
                fee += tier_size * price;
                price *= cpp_rational { 6, 5 };
                bytes -= tier_size;
            }
            fee += bytes * price;
            return numerator(fee) / denominator(fee);
        }

        cpp_int minimum_fee(uint32_t tx_size, uint8_t era, const ex_units &units, size_t ref_bytes) const
        {
            cpp_int fee = cpp_int { _a } * tx_size + _b;
            // ceil(mem * price_mem + steps * price_steps), with the common
            // denominator prepared once. No rational normalization per transaction.
            const cpp_int num = units.mem * _mem_num + units.steps * _steps_num;
            fee += (num + _execution_den - 1) / _execution_den;
            if (era >= 7)
                fee += reference_script_fee(ref_bytes);
            return fee;
        }

    private:
        static constexpr size_t tier_size = 25'600;
        uint64_t _a, _b;
        cpp_int _mem_num, _steps_num, _execution_den;
        // Includes the exact upper limit of 200 KiB; larger inputs use the fallback.
        std::array<cpp_rational, 9> _prices {}, _prefixes {};
    };
}
