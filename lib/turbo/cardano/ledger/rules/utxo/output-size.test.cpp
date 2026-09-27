/* Copyright (c) 2026 R2 Rationality OÜ. License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */
#include "output-size.hpp"
#include <array>
#include <boost/multiprecision/cpp_int.hpp>
#include <turbo/common/test.hpp>

using namespace turbo;
using cardano::ledger::rules::output_size_limits;

suite ledger_output_size_suite = [] {
    "UTXO output reduction matches every minimum-coin premise"_test = [] {
        output_size_limits limits {};
        cardano::protocol_params params {};
        params.max_value_size = UINT32_MAX;
        // Include unequal ratios, equality, and products exceeding uint64_t.
        const std::array<std::pair<uint64_t, uint32_t>, 3> outputs {{
            { 1000, 40 }, { 1801, 140 }, { UINT64_MAX, UINT32_MAX }
        }};
        for (const auto &[coin, size]: outputs) {
            cardano::tx_out_data out {};
            out.coin = coin;
            limits.observe(out, size);
        }
        for (const uint64_t parameter: { uint64_t { 0 }, uint64_t { 5 }, uint64_t { 6 }, UINT64_MAX }) {
            params.lovelace_per_utxo_byte = parameter;
            bool satisfies_all = true;
            for (const auto &[coin, size]: outputs)
                satisfies_all &= boost::multiprecision::uint128_t { coin }
                    >= boost::multiprecision::uint128_t { parameter } * (uint64_t { 160 } + size);
            if (satisfies_all)
                expect(nothrow([&] { limits.validate(params); }));
            else
                expect(throws([&] { limits.validate(params); }));
        }
    };

    "UTXO maximum value size includes the CBOR integer header"_test = [] {
        cardano::protocol_params params {};
        params.lovelace_per_utxo_byte = 0;
        params.max_value_size = 1;
        cardano::tx_out_data out {};
        out.coin = 23;
        output_size_limits limits {};
        limits.observe(out, 40);
        expect(nothrow([&] { limits.validate(params); }));
        out.coin = 24; // CBOR integer encoding grows from one byte to two.
        limits.observe(out, 40);
        expect(throws([&] { limits.validate(params); }));
        params.max_value_size = 2;
        expect(nothrow([&] { limits.validate(params); }));
    };
};
