/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <limits>
#include <turbo/common/test.hpp>
#include <turbo/plutus/context.hpp>

using namespace turbo;
using namespace turbo::cardano;
using namespace turbo::plutus;

suite plutus_context_suite = [] {
    "plutus::context"_test = [] {
        "total redeemer budget"_test = [] {
            const context::redeemer_map redeemers {
                { redeemer_id { redeemer_tag::spend, 0 },
                    tx_redeemer { redeemer_tag::spend, 0, {}, ex_units { 4, 7 } } },
                { redeemer_id { redeemer_tag::mint, 0 },
                    tx_redeemer { redeemer_tag::mint, 0, {}, ex_units { 6, 13 } } }
            };
            expect_equal(ex_units { 10, 20 }, context::validate_redeemer_budgets(redeemers, { 10, 20 }));
            expect(throws([&] { context::validate_redeemer_budgets(redeemers, { 9, 20 }); }));
            expect(throws([&] { context::validate_redeemer_budgets(redeemers, { 10, 19 }); }));
        };
        "total redeemer budget cannot overflow"_test = [] {
            const auto max = std::numeric_limits<uint64_t>::max();
            const context::redeemer_map redeemers {
                { redeemer_id { redeemer_tag::spend, 0 },
                    tx_redeemer { redeemer_tag::spend, 0, {}, ex_units { max, max } } },
                { redeemer_id { redeemer_tag::mint, 0 },
                    tx_redeemer { redeemer_tag::mint, 0, {}, ex_units { 1, 1 } } }
            };
            expect(throws([&] { context::validate_redeemer_budgets(redeemers, { max, max }); }));
        };
    };
};