/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include "test.hpp"
#include "numeric-cast.hpp"

namespace {
    using namespace turbo;
}

suite turbo_common_numeric_cast_suite = [] {
    "turbo::common::numeric_cast"_test = [] {
        // both unsigned
        expect_equal(uint8_t{0}, numeric_cast<uint8_t>(uint64_t{0}));
        expect_equal(uint8_t{24}, numeric_cast<uint8_t>(uint64_t{24}));
        expect_equal(uint8_t{255}, numeric_cast<uint8_t>(uint64_t{255}));
        expect(throws([&] { numeric_cast<uint8_t>(256); }));

        // both signed
        expect_equal(int8_t{-128}, numeric_cast<int8_t>(int64_t{-128}));
        expect_equal(int8_t{24}, numeric_cast<int8_t>(int64_t{24}));
        expect_equal(int8_t{127}, numeric_cast<int8_t>(int64_t{127}));
        expect(throws([&] { numeric_cast<int8_t>(int64_t{128}); }));
        expect(throws([&] { numeric_cast<int8_t>(int64_t{-129}); }));

        // differing signedness
        expect(throws([&] { numeric_cast<uint64_t>(int64_t{-1}); }));
        expect_equal(uint8_t{0}, numeric_cast<uint8_t>(int8_t{0}));
        expect_equal(int8_t{0}, numeric_cast<int8_t>(uint8_t{0}));
        expect_equal(uint8_t{255}, numeric_cast<uint8_t>(int64_t{255}));
        expect(throws([&] { numeric_cast<uint8_t>(int64_t{256}); }));
    };
};