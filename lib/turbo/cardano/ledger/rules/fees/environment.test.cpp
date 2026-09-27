/* Copyright (c) 2026 R2 Rationality OÜ. License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */
#include "environment.hpp"
#include <turbo/common/test.hpp>

namespace {
    using namespace turbo;
    using namespace cardano;

    // Deliberately retain the unprepared rational definition as an independent oracle.
    cpp_int fee_oracle(const protocol_params &params, uint32_t size, uint8_t era,
        const ex_units &units, size_t bytes)
    {
        cpp_int fee = cpp_int { params.min_fee_a } * size + params.min_fee_b;
        const cpp_rational execution = cpp_rational { units.mem } * rational_from_r64(params.ex_unit_prices.mem)
            + cpp_rational { units.steps } * rational_from_r64(params.ex_unit_prices.steps);
        fee += (numerator(execution) + denominator(execution) - 1) / denominator(execution);
        if (era >= 7) {
            cpp_rational refs = 0;
            auto price = rational_from_r64(params.min_fee_ref_script_cost_per_byte);
            while (bytes >= 25'600) {
                refs += 25'600 * price;
                price *= cpp_rational { 6, 5 };
                bytes -= 25'600;
            }
            refs += bytes * price;
            fee += numerator(refs) / denominator(refs);
        }
        return fee;
    }
}

suite fee_environment_suite = [] {
    "Fees prepared arithmetic matches exact rational definition at tier edges"_test = [] {
        protocol_params params {};
        params.min_fee_a = 44;
        params.min_fee_b = 155381;
        params.ex_unit_prices = { { 1, 3 }, { 2, 7 } };
        params.min_fee_ref_script_cost_per_byte = { 7, 11 };
        const ledger::rules::fee_environment env { params };
        for (const uint8_t era: { 6, 7 }) {
            for (const auto units: { ex_units {}, ex_units { 1, 1 }, ex_units { UINT64_MAX, UINT64_MAX } }) {
                for (const size_t bytes: { 0U, 1U, 25'599U, 25'600U, 25'601U, 204'799U, 204'800U,
                        204'801U, 230'400U, 256'001U }) {
                    expect(env.minimum_fee(1000, era, units, bytes) == fee_oracle(params, 1000, era, units, bytes));
                }
            }
        }
    };

    "Fees combine execution prices before rounding and replace all parameter inputs"_test = [] {
        protocol_params params {};
        params.ex_unit_prices = { { 1, 3 }, { 1, 3 } };
        params.min_fee_ref_script_cost_per_byte = { 1, 3 };
        const ledger::rules::fee_environment before { params };
        expect(before.minimum_fee(0, 7, { 1, 1 }, 1) == 1);
        params.min_fee_a = 2;
        params.min_fee_b = 3;
        params.ex_unit_prices = { { 2, 3 }, { 2, 3 } };
        params.min_fee_ref_script_cost_per_byte = { 4, 3 };
        const ledger::rules::fee_environment after { params };
        expect(after.minimum_fee(10, 7, { 1, 1 }, 1) == 26);
        expect(before.minimum_fee(0, 7, { 1, 1 }, 1) == 1);
    };
};
