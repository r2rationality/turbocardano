/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com) */

#include "test.hpp"
#include "pool-allocator.hpp"
#include <map>
#include <string>

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

        "std map adapter"_test = [] {
            using value_type = std::pair<const size_t, std::string>;
            using allocator_type = pool_allocator_t<value_type, 4, true>;
            using map_type = std::map<size_t, std::string, std::less<size_t>, allocator_type>;

            map_type values {};
            auto [first, created] = values.try_emplace(1, "one");
            expect(created);
            const auto first_slot = reinterpret_cast<uintptr_t>(&*first);
            values.erase(first);
            auto [second, second_created] = values.try_emplace(2, "two");
            expect(second_created);
            expect(first_slot == reinterpret_cast<uintptr_t>(&*second));

            map_type copy { values };
            expect(copy == values);
            expect(copy.get_allocator() != values.get_allocator());
            {
                map_type retired {};
                retired.swap(copy);
                retired.get_allocator().begin_bulk_release();
            }
            expect(copy.empty());
            expect(values.at(2) == "two");

            const auto alloc_before_move = values.get_allocator();
            map_type moved { std::move(values) };
            expect(moved.get_allocator() == alloc_before_move);
            expect(moved.at(2) == "two");

            {
                map_type retired {};
                retired.swap(moved);
                retired.get_allocator().begin_bulk_release();
            }
            expect(moved.empty());
            moved.try_emplace(3, "three");
            expect(moved.at(3) == "three");
        };

        "std map move assignment"_test = [] {
            using value_type = std::pair<const size_t, std::string>;
            using allocator_type = pool_allocator_t<value_type, 4, true>;
            using map_type = std::map<size_t, std::string, std::less<size_t>, allocator_type>;

            map_type source {};
            source.try_emplace(1, "one");
            const auto source_alloc = source.get_allocator();
            map_type target {};
            target = std::move(source);

            expect(target.get_allocator() == source_alloc);
            expect(source.get_allocator() == source_alloc);
            expect(target.at(1) == "one");
            expect(source.empty());
            source.try_emplace(2, "two");
            expect(source.at(2) == "two");
        };
    };
};
