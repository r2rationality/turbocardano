/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com) */

#include "test.hpp"
#include "pool-allocator.hpp"

namespace {
    using namespace turbo;
}

suite turbo_common_pool_allocator_suite = [] {
    "turbo::common::pool_allocator"_test = [] {
        "trivial"_test = [] {
            static constexpr size_t batch_size = 4;
            pool_allocator_t<size_t, batch_size> alloc {};
            std::set<size_t *> known {};
            for (size_t i = 0; i < batch_size * 2; ++i) {
                auto ptr = alloc.allocate();
                expect(!known.contains(ptr));
                known.emplace(ptr);
            }
            expect_equal(size_t{batch_size * 2U}, known.size());
            for (auto &ptr: known)
                alloc.deallocate(ptr);
            for (size_t i = 0; i < batch_size * 2; ++i) {
                auto ptr = alloc.allocate();
                expect(known.contains(ptr));
            }
            expect(!known.contains(alloc.allocate()));
        };

        "make_ptr"_test = [] {
            static constexpr size_t batch_size = 4;
            pool_allocator_t<size_t, batch_size> alloc {};
            size_t *raw = nullptr;
            {
                auto ptr = alloc.make_ptr(size_t{42});
                expect_equal(size_t{42}, *ptr);
                raw = ptr.get();
            }
            // deleter must have returned the slot to _free
            expect(raw == alloc.allocate());
        };

        "SKIP_DTOR opt-in"_test = [] {
            struct non_trivial {
                ~non_trivial() {}
            };
            // must compile with explicit opt-in
            pool_allocator_t<non_trivial, 4, true> alloc {};
            auto p = alloc.allocate();
            expect(p != nullptr);
        };

        "make_ptr exception safety"_test = [] {
            struct throws_on_construct {
                throws_on_construct() { throw std::runtime_error("oops"); }
            };
            pool_allocator_t<throws_on_construct, 4, true> alloc {};
            expect(throws([&] { alloc.make_ptr(); }));
            expect_equal(size_t{1}, alloc.free_count()); // slot was returned
        };

        "multi-arena"_test = [] {
            static constexpr size_t batch_size = 4;
            pool_allocator_t<size_t, batch_size> alloc {};
            std::set<size_t *> ptrs {};
            // span three arenas
            for (size_t i = 0; i < batch_size * 3; ++i)
                ptrs.emplace(alloc.allocate());
            // all pointers must be unique and non-null
            expect_equal(size_t{batch_size * 3U}, ptrs.size());
        };
    };
};
