/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com) */

#include "test.hpp"
#include "pool-allocator.hpp"
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>

namespace {
    using namespace turbo;

    struct counting_resource: std::pmr::memory_resource {
        size_t bytes = 0;
    private:
        void *do_allocate(const size_t n, const size_t alignment) override
        {
            auto *ptr = std::pmr::new_delete_resource()->allocate(n, alignment);
            bytes += n;
            return ptr;
        }

        void do_deallocate(void *ptr, const size_t n, const size_t alignment) override
        {
            std::pmr::new_delete_resource()->deallocate(ptr, n, alignment);
            bytes -= n;
        }

        bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override
        {
            return this == &other;
        }
    };
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

        "copies share the resource"_test = [] {
            pool_allocator_t<size_t, 4> alloc {};
            auto copy = alloc;
            auto *ptr = alloc.allocate();
            copy.deallocate(ptr);
            expect(ptr == alloc.allocate());
        };

        "make_ptr destroys nested objects when requested"_test = [] {
            struct node_t;
            using allocator_type = pool_allocator_t<node_t, 4, false>;
            struct node_t {
                size_t &destroy_count;
                typename allocator_type::ptr_t child {};

                explicit node_t(size_t &count): destroy_count { count }
                {
                }

                ~node_t()
                {
                    ++destroy_count;
                }
            };

            size_t destroy_count = 0;
            allocator_type alloc {};
            {
                auto root = alloc.make_ptr(destroy_count);
                root->child = alloc.make_ptr(destroy_count);
            }
            expect_equal(size_t{2}, destroy_count);
            expect_equal(size_t{2}, alloc.free_count());
        };

        "destructor-enabled pointer keeps the resource alive"_test = [] {
            struct value_t {
                size_t &destroy_count;

                explicit value_t(size_t &count): destroy_count { count }
                {
                }

                ~value_t()
                {
                    ++destroy_count;
                }
            };

            using allocator_type = pool_allocator_t<value_t, 4>;
            size_t destroy_count = 0;
            allocator_type::ptr_t ptr {};
            {
                allocator_type alloc {};
                ptr = alloc.make_ptr(destroy_count);
            }
            expect_equal(size_t{0}, destroy_count);
            ptr.reset();
            expect_equal(size_t{1}, destroy_count);
        };

        "make_ptr skips pool-owned object graphs"_test = [] {
            struct node_t;
            using allocator_type = pool_allocator_t<node_t, 4, true>;
            struct node_t {
                size_t &destroy_count;
                typename allocator_type::ptr_t child {};

                explicit node_t(size_t &count): destroy_count { count }
                {
                }

                ~node_t()
                {
                    ++destroy_count;
                }
            };

            size_t destroy_count = 0;
            allocator_type alloc {};
            auto root = alloc.make_ptr(destroy_count);
            root->child = alloc.make_ptr(destroy_count);
            const std::set<node_t *> before { root.get(), root->child.get() };

            root.reset();
            alloc.recycle_all();
            expect_equal(size_t{0}, destroy_count);

            {
                auto first = alloc.make_ptr(destroy_count);
                auto second = alloc.make_ptr(destroy_count);
                const std::set<node_t *> after { first.get(), second.get() };
                expect(before == after);
            }
            expect_equal(size_t{0}, destroy_count);
            expect_equal(size_t{2}, alloc.free_count());
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

        "bulk recycle"_test = [] {
            static constexpr size_t batch_size = 4;
            pool_allocator_t<size_t, batch_size> alloc {};
            std::set<size_t *> before {};
            for (size_t i = 0; i < batch_size * 3; ++i)
                before.emplace(alloc.allocate());

            alloc.recycle_all();

            std::set<size_t *> after {};
            for (size_t i = 0; i < batch_size * 3; ++i)
                after.emplace(alloc.allocate());
            expect(before == after);
        };

    };

    "turbo::common::pmr_pool_allocator"_test = [] {
        "rebind, alignment and allocation size"_test = [] {
            struct alignas(64) item { size_t value; };
            pmr_pool_allocator_t<item> alloc {};
            pmr_pool_allocator_t<std::byte> rebound { alloc };
            pmr_pool_allocator_t<item> copy { rebound };
            expect(copy == alloc);
            auto *ptr = alloc.allocate(3);
            expect(reinterpret_cast<uintptr_t>(ptr) % alignof(item) == 0);
            std::construct_at(ptr + 2, item { 42 });
            expect(ptr[2].value == 42_ul);
            std::destroy_at(ptr + 2);
            copy.deallocate(ptr, 3);
            expect(throws<std::bad_array_new_length>([&] {
                constexpr auto too_many = std::numeric_limits<size_t>::max() / sizeof(item) + 1;
                auto *unexpected = alloc.allocate(too_many);
                alloc.deallocate(unexpected, too_many);
            }));
        };

        "container ownership"_test = [] {
            using alloc_type = pmr_pool_allocator_t<std::pair<const int, std::shared_ptr<int>>>;
            using map_type = std::map<int, std::shared_ptr<int>, std::less<int>, alloc_type>;
            counting_resource upstream {};
            std::weak_ptr<int> value;
            {
                map_type result { alloc_type { &upstream } };
                {
                    map_type source { alloc_type { &upstream } };
                    source.emplace(1, std::make_shared<int>(42));
                    value = source.at(1);
                    const auto *node = &*source.begin();
                    auto copy = source;
                    expect(copy == source);
                    expect(copy.get_allocator() != source.get_allocator());
                    const auto copy_alloc = copy.get_allocator();

                    const auto result_alloc = result.get_allocator();
                    result = source;
                    expect(result == source);
                    expect(result.get_allocator() == result_alloc);
                    const auto *copy_node = &*copy.begin();
                    result.swap(copy);
                    expect(result.get_allocator() == copy_alloc);
                    expect(copy.get_allocator() == result_alloc);
                    expect(&*result.begin() == copy_node);

                    auto moved = std::move(source);
                    source.emplace(2, std::make_shared<int>(2));
                    result = std::move(moved);
                    moved.emplace(3, std::make_shared<int>(3));
                    expect(&*result.begin() == node);
                    expect(result.get_allocator() == source.get_allocator());
                }
                expect(*result.at(1) == 42_i);
                expect(upstream.bytes > 0);
                result.clear();
                expect(value.expired());
            }
            expect(upstream.bytes == 0_ul);
        };
    };
};
