/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include "test.hpp"
#include "pool-allocator.hpp"

namespace {
    using namespace turbo;
}

suite turbo_common_pool_allocator_suite = [] {
    "turbo::common::pool_allocator"_test = [] {
        static constexpr size_t batch_size = 4;
        pool_allocator_t<size_t, batch_size> alloc {};
        std::set<size_t *> known {};
        for (size_t i = 0; i < batch_size * 2; ++i) {
            auto ptr = alloc.allocate();
            expect(!known.contains(ptr));
            known.emplace(ptr);
        }
        expect_equal(batch_size * 2, known.size());
        for (auto &ptr: known)
            alloc.deallocate(ptr);
        for (size_t i = 0; i < batch_size * 2; ++i) {
            auto ptr = alloc.allocate();
            expect(known.contains(ptr));
        }
        expect(!known.contains(alloc.allocate()));
    };
};
