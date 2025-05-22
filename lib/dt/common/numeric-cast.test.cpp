/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include "test.hpp"
#include "numeric-cast.hpp"

namespace {
    using namespace daedalus_turbo;
}

suite turbo_common_numeric_cast_suite = [] {
    "turbo::common::numeric_cast"_test = [] {
        expect_equal(uint8_t { 24 }, numeric_cast<uint8_t>(uint64_t { 24 }));
        expect_equal(int8_t { 24 }, numeric_cast<int8_t>(int64_t { 24 }));
        expect_equal(int8_t { -24 }, numeric_cast<int8_t>(int64_t { -24 }));
        expect_equal(uint8_t { 255 }, numeric_cast<uint8_t>(int16_t { 255 }));
        expect_equal(int64_t { 205665 }, numeric_cast<int64_t>(uint64_t { 205665 }));
        expect_equal(uint64_t { std::numeric_limits<int64_t>::max() }, numeric_cast<uint64_t>(std::numeric_limits<int64_t>::max()));
        expect(throws([&] { numeric_cast<uint8_t>(256); }));
        expect(throws([&] { numeric_cast<uint8_t>(256); }));
        expect(throws([&] { numeric_cast<int8_t>(-129); }));
        expect(throws([&] { numeric_cast<uint64_t>(int64_t { -250 }); }));
        expect(throws([&] { numeric_cast<int8_t>(int64_t { -250 }); }));
        expect(throws([&] { numeric_cast<int8_t>(int64_t { 250 }); }));
        expect(throws([&] { numeric_cast<int64_t>(std::numeric_limits<uint64_t>::max()); }));
    };
};