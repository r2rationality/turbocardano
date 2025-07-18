/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include "test.hpp"
#include "scope-exit.hpp"

namespace {
    using namespace turbo;
}

suite turbo_common_scope_exit_suite = [] {
    "turbo::common::scope_exit"_test = [] {
        "normal"_test = [] {
            size_t val = 1U;
            {
                scope_exit cleanup{[&]{ --val;}};
                expect_equal(1U, val);
            }
            expect_equal(0U, val);
        };
        "move"_test = [] {
            size_t val = 1U;
            {
                scope_exit cleanup1{[&]{ --val;}};
                scope_exit cleanup2{std::move(cleanup1)};
                expect_equal(1U, val);
            }
            expect_equal(0U, val);
        };
        "release"_test = [] {
            size_t val = 1U;
            {
                scope_exit cleanup{[&]{ --val;}};
                expect_equal(1U, val);
                cleanup.release();
                expect_equal(1U, val);
            }
            expect_equal(1U, val);
        };
    };
};