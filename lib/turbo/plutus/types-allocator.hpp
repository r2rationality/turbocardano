#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <cstddef>
#include <initializer_list>
#include <map>
#include <memory>
#include <memory_resource>
#include <new>
#include <string_view>
#include <utility>
#include <vector>
#include <turbo/common/logger.hpp>
#include <turbo/util.hpp>

namespace turbo::plutus {
    using error = turbo::error;

    // The idea behind the faster allocation is to release all objects at once
    // and save on the incremental de-allocation and calls to destructors.
    // For that to work all internal objects must be allocated using the same allocator,
    // so that there are no memory leaks.
    struct allocator {
        using allocator_type = std::pmr::polymorphic_allocator<std::byte>;

        template<typename T>
        struct ptr_type {
            ptr_type() =default;

            ptr_type(const T *ptr): _ptr { ptr }
            {
            }

            const T *get() const
            {
                return _ptr;
            }

            const T *operator->() const
            {
                return _ptr;
            }

            const T &operator*() const
            {
                return *_ptr;
            }

            operator bool() const
            {
                return _ptr;
            }
        private:
            const T *_ptr = nullptr;
        };

        allocator(const allocator &) =delete;
        allocator &operator=(const allocator &) =delete;
        allocator &operator=(allocator &&o) =delete;

        allocator():
            _mr { std::make_unique<std::pmr::monotonic_buffer_resource>(0x800000, my_resource::get()) },
            _ptrs { _mr.get() }
        {
        }

        allocator(allocator &&o):
            _mr { std::move(o._mr) },
            _ptrs { std::move(o._ptrs), _mr.get() }
        {
        }

        ~allocator()
        {
            for (const auto &[p, dtr]: _ptrs)
                dtr(p);
        }

        template<typename T, typename... Args>
        ptr_type<T> make(Args&&... a);

        template<typename T, typename... Args>
        ptr_type<T> make_foreign(Args&&... a);

        template<typename T>
        void register_destructor(const ptr_type<T> &ptr);

        std::pmr::memory_resource *resource()
        {
            return _mr.get();
        }
    private:
        struct any_ptr {
            const void *ptr = nullptr;
            void(*dtr)(const void*);
        };

        struct my_resource: std::pmr::memory_resource {
            using my_alloc = std::allocator<std::byte>;

            static my_resource *get()
            {
                static my_resource mr {};
                return &mr;
            }

            void *do_allocate(const size_t bytes, const size_t align) override
            {
                const auto aligned_size = _aligned_bytes(bytes, align);
                return _alloc.allocate(aligned_size);
            }

            void do_deallocate(void *ptr, const size_t bytes, const size_t align) override
            {
                const auto aligned_size = _aligned_bytes(bytes, align);
                _alloc.deallocate(reinterpret_cast<std::byte *>(ptr), aligned_size);
            }

            bool do_is_equal(const memory_resource &o) const noexcept override
            {
                return this == &o;
            }
        private:
            static constexpr size_t _aligned_bytes(const size_t bytes, const size_t align)
            {
                const auto mask = align - 1;
                return (bytes + mask) & ~mask;
            }

            my_alloc _alloc {};
        };

        struct counting_resource: std::pmr::memory_resource {
            using my_alloc = std::allocator<std::byte>;

            counting_resource(memory_resource *upstream): _upstream { upstream }
            {
                if (!_upstream) [[unlikely]]
                    throw error("counting resource requires a not-null upstream memory resource!");
            }

            void *do_allocate(const size_t bytes, const size_t align) override
            {
                const auto aligned_size = _aligned_bytes(bytes, align);
                _size += aligned_size;
                ++_cnts[aligned_size].num_allocs;
                return _mbr.allocate(aligned_size);
            }

            void do_deallocate(void *ptr, const size_t bytes, const size_t align) override
            {
                const auto aligned_size = _aligned_bytes(bytes, align);
                if (aligned_size > _size) [[unlikely]]
                    throw error("trying to deallocate more than has been allocated!");
                _size -= aligned_size;
                ++_cnts[aligned_size].num_deallocs;
                _mbr.deallocate(ptr, aligned_size);
            }

            bool do_is_equal(const memory_resource &o) const noexcept override
            {
                return this == &o;
            }

            void log_stats(const std::string_view context) const
            {
                logger::debug("{}: memory usage: {} bytes", context, _size);
            }
        private:
            static constexpr size_t _aligned_bytes(const size_t bytes, const size_t align)
            {
                const auto mask = align - 1;
                return (bytes + mask) & ~mask;
            }

            struct info_t {
                size_t num_allocs = 0;
                size_t num_deallocs = 0;
            };

            memory_resource *_upstream;
            std::pmr::monotonic_buffer_resource _mbr { _upstream };
            size_t _size = 0;
            std::map<size_t, info_t> _cnts {};
        };

        std::unique_ptr<std::pmr::memory_resource> _mr;
        std::pmr::vector<any_ptr> _ptrs;
    };

    template<typename T>
    struct list_type: std::pmr::vector<T> {
        using base_type = std::pmr::vector<T>;

        list_type() =delete;

        list_type(allocator &alloc): base_type { alloc.resource() }
        {
        }

        list_type(allocator &alloc, std::initializer_list<T> il): base_type { il, alloc.resource() }
        {
        }

        list_type(allocator &alloc, list_type<T> &&l): base_type { std::move(l), alloc.resource() }
        {
        }
    };

    template<typename T>
    struct map_type: std::pmr::vector<T> {
        using base_type = std::pmr::vector<T>;

        map_type() =delete;

        map_type(allocator &alloc): base_type { alloc.resource() }
        {
        }

        map_type(allocator &alloc, std::initializer_list<T> il): base_type { il, alloc.resource() }
        {
        }

        map_type(allocator &alloc, map_type<T> &&l): base_type { std::move(l), alloc.resource() }
        {
        }
    };

#if defined(__GNUC__) && !defined(__clang__)
    // GCC 13 reacts oddly to the libc++ implementation of std::pmr::string.
#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
    template<typename T, typename... Args>
    allocator::ptr_type<T> allocator::make(Args &&...a)
    {
        T *p = new (_mr->allocate(sizeof(T), alignof(T))) T { std::forward<Args>(a)... };
        return p;
    }

    template<typename T, typename... Args>
    allocator::ptr_type<T> allocator::make_foreign(Args &&...a)
    {
        auto ptr = make<T>(std::forward<Args>(a)...);
        register_destructor(ptr);
        return ptr;
    }

    template<typename T>
    void allocator::register_destructor(const ptr_type<T> &ptr)
    {
        try {
            _ptrs.emplace_back(ptr.get(), [](const void* x) { static_cast<const T*>(x)->~T(); });
        } catch (...) {
            // Release memory owned by alternative allocators. The monotonic resource itself is
            // still released as a whole at the end of this allocator's lifetime.
            ptr.get()->~T();
            throw;
        }
    }
#if defined(__GNUC__) && !defined(__clang__)
#   pragma GCC diagnostic pop
#endif
}
