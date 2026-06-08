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

        // widening casts — always valid
        expect_equal(int64_t{127}, numeric_cast<int64_t>(int8_t{127}));
        expect_equal(uint64_t{255}, numeric_cast<uint64_t>(uint8_t{255}));

        // same-size same-signedness — no-op
        expect_equal(int32_t{42}, numeric_cast<int32_t>(int32_t{42}));

        // unsigned -> signed, upper overflow
        expect_equal(int8_t{127}, numeric_cast<int8_t>(uint8_t{127}));
        expect(throws([&] { numeric_cast<int8_t>(uint8_t{128}); }));

        // unsigned -> signed, same width
        expect_equal(int64_t{0}, numeric_cast<int64_t>(uint64_t{0}));
        expect(throws([&] { numeric_cast<int64_t>(std::numeric_limits<uint64_t>::max()); }));

        // signed narrow -> unsigned wide (digits-sensitive path: all non-negative values fit)
        expect_equal(uint32_t{32767}, numeric_cast<uint32_t>(int16_t{32767}));
        expect(throws([&] { numeric_cast<uint32_t>(int16_t{-1}); }));

        // signed -> unsigned, FROM only slightly wider than TO
        expect_equal(uint8_t{255}, numeric_cast<uint8_t>(int16_t{255}));
        expect(throws([&] { numeric_cast<uint8_t>(int16_t{256}); }));
    };
};