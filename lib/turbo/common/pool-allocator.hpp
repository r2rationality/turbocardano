#pragma once
/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com) */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <memory_resource>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace turbo {
    enum class zero_policy_t: uint8_t {
        none = 0,
        all = 1
    };

    template<typename T, zero_policy_t>
    struct zero_policy_type_ok_t: std::bool_constant<std::is_trivially_copyable_v<T>> {};

    template<typename T>
    struct zero_policy_type_ok_t<T, zero_policy_t::none>: std::true_type {};

    namespace detail {
        template<typename T, size_t BATCH_SZ, zero_policy_t ZERO_POLICY>
        struct pool_allocator_resource_t {
            static_assert(BATCH_SZ > 0, "a pool allocator batch must contain at least one slot");

            pool_allocator_resource_t() =default;
            pool_allocator_resource_t(const pool_allocator_resource_t &) = delete;
            pool_allocator_resource_t(pool_allocator_resource_t &&) = delete;
            pool_allocator_resource_t &operator=(const pool_allocator_resource_t &) = delete;
            pool_allocator_resource_t &operator=(pool_allocator_resource_t &&) = delete;

            [[nodiscard]] T *allocate()
            {
                T *ptr = nullptr;
                if (_free) {
                    ptr = static_cast<T *>(_free);
                    std::memcpy(&_free, static_cast<const void *>(ptr), sizeof(_free));
                    --_free_count;
                } else {
                    if (_arenas.empty()) [[unlikely]] {
                        _add_arena();
                    } else if (_arena_offset == _arenas[_arena_index].capacity) [[unlikely]] {
                        if (_arena_index + 1 == _arenas.size()) {
                            _add_arena();
                        } else {
                            ++_arena_index;
                            _arena_offset = 0;
                        }
                    }
                    auto *arena = static_cast<std::byte *>(_arenas[_arena_index].data);
                    ptr = reinterpret_cast<T *>(arena + _arena_offset++ * _stride);
                }
                if constexpr (ZERO_POLICY == zero_policy_t::all)
                    std::memset(ptr, 0, sizeof(T));
                return ptr;
            }

            void deallocate(T *ptr) noexcept
            {
                // Reuse the slot's raw storage for the next free-list pointer.
                std::memcpy(static_cast<void *>(ptr), &_free, sizeof(_free));
                _free = ptr;
                ++_free_count;
            }

            [[nodiscard]] size_t free_count() const noexcept
            {
                return _free_count;
            }

            void recycle_all() noexcept
            {
                _free = nullptr;
                _free_count = 0;
                _arena_index = 0;
                _arena_offset = 0;
            }

            ~pool_allocator_resource_t()
            {
                for (const auto &arena: _arenas)
                    operator delete(arena.data, std::align_val_t { _alignment });
            }

        private:
            struct arena_t {
                void *data;
                size_t capacity;
            };

            static constexpr size_t _align_up(const size_t size, const size_t alignment)
            {
                const auto rem = size % alignment;
                if (!rem) [[likely]]
                    return size;
                const auto padding = alignment - rem;
                if (size > std::numeric_limits<size_t>::max() - padding) [[unlikely]]
                    throw std::bad_array_new_length {};
                return size + padding;
            }

            static constexpr size_t _alignment = std::max(alignof(T), alignof(void *));
            static constexpr size_t _stride = _align_up(std::max(sizeof(T), sizeof(void *)), _alignment);
            std::vector<arena_t> _arenas {};
            void *_free = nullptr;
            size_t _free_count = 0;
            size_t _arena_index = 0;
            size_t _arena_offset = 0;
            size_t _next_arena_capacity = 1;

            void _add_arena()
            {
                const auto capacity = _next_arena_capacity;
                if (_stride > std::numeric_limits<size_t>::max() / capacity) [[unlikely]]
                    throw std::bad_array_new_length {};
                const auto arena_size = capacity * _stride;
                auto *arena = operator new(arena_size, std::align_val_t { _alignment });
                try {
                    _arenas.push_back({ arena, capacity });
                } catch (...) {
                    operator delete(arena, std::align_val_t { _alignment });
                    throw;
                }
                _arena_index = _arenas.size() - 1;
                _arena_offset = 0;
                _next_arena_capacity = capacity >= (BATCH_SZ + 1) / 2
                    ? BATCH_SZ
                    : capacity * 2;
            }
        };
    }

    // A single-type recyclable object pool. Each allocation reserves exactly
    // one T; this is intentionally not a standard-container allocator.
    // Pool copies share their resource.
    // SKIP_DTOR applies to ptr_t: when enabled, destroying a pointer recycles
    // its storage without invoking T's destructor. Such pointers hold a
    // non-owning resource reference, so at least one allocator sharing that
    // resource must outlive them. This permits pool-owned object graphs to be
    // discarded in bulk without walking them or creating a reference cycle.
    // The resource itself owns raw storage and never discovers live objects.
    template<typename T, size_t BATCH_SZ = 0x1000, bool SKIP_DTOR = std::is_trivially_destructible_v<T>, zero_policy_t ZERO_POLICY = zero_policy_t::none>
    struct pool_allocator_t {
        static_assert(zero_policy_type_ok_t<T, ZERO_POLICY>::value,
            "pool_allocator_t zeroing is only supported for trivially copyable T");

        pool_allocator_t(): _resource { std::make_shared<resource_type>() }
        {
        }

        pool_allocator_t(const pool_allocator_t &) noexcept =default;
        pool_allocator_t(pool_allocator_t &&) noexcept =default;
        pool_allocator_t &operator=(const pool_allocator_t &) noexcept =default;
        pool_allocator_t &operator=(pool_allocator_t &&) noexcept =default;

        struct deleter_t {
            using resource_type = detail::pool_allocator_resource_t<T, BATCH_SZ, ZERO_POLICY>;
            using resource_ref_type = std::conditional_t<
                SKIP_DTOR, resource_type *, std::shared_ptr<resource_type>>;
            resource_ref_type resource {};

            void operator()(T *ptr) const noexcept
            {
                if (!ptr) [[unlikely]]
                    return;
                if constexpr (!SKIP_DTOR)
                    std::destroy_at(ptr);
                if (resource) [[likely]]
                    resource->deallocate(ptr);
            }
        };

        using ptr_t = std::unique_ptr<T, deleter_t>;

        [[nodiscard]] T *allocate()
        {
            return _get_resource().allocate();
        }

        void deallocate(T *ptr) noexcept
        {
            if (!ptr) [[unlikely]]
                return;
            if (!_resource) [[unlikely]]
                std::terminate();
            _resource->deallocate(ptr);
        }

        template<typename... Args>
        ptr_t make_ptr(Args&&... args)
        {
            T* raw = allocate();
            try {
                new (raw) T { std::forward<Args>(args)... };
            } catch (...) {
                deallocate(raw);
                throw;
            }
            if constexpr (SKIP_DTOR)
                return { raw, deleter_t { _resource.get() } };
            else
                return { raw, deleter_t { _resource } };
        }

        [[nodiscard]] size_t free_count() const noexcept
        {
            return _resource ? _resource->free_count() : 0;
        }

        // Invalidates every allocation and makes all existing arena slots
        // reusable. No external owner whose deleter can run later may survive
        // this call.
        void recycle_all() noexcept
        {
            if (!_resource) [[unlikely]]
                std::terminate();
            _resource->recycle_all();
        }

    private:
        using resource_type = detail::pool_allocator_resource_t<T, BATCH_SZ, ZERO_POLICY>;
        std::shared_ptr<resource_type> _resource {};

        resource_type &_get_resource()
        {
            if (!_resource) [[unlikely]]
                _resource = std::make_shared<resource_type>();
            return *_resource;
        }
    };

    // Copies and rebinds share an unsynchronized pool; their allocations and
    // deallocations must not overlap across threads. Container copies get fresh pools.
    template<typename T>
    struct pmr_pool_allocator_t {
        using value_type = T;
        using size_type = size_t;
        using difference_type = std::ptrdiff_t;
        using propagate_on_container_move_assignment = std::true_type;
        using propagate_on_container_swap = std::true_type;
        using is_always_equal = std::false_type;

        template<typename U>
        struct rebind {
            using other = pmr_pool_allocator_t<U>;
        };

        explicit pmr_pool_allocator_t(std::pmr::memory_resource *upstream=std::pmr::get_default_resource())
            : _resource { std::make_shared<std::pmr::unsynchronized_pool_resource>(upstream) }
        {
        }

        pmr_pool_allocator_t(const pmr_pool_allocator_t &) noexcept =default;
        pmr_pool_allocator_t &operator=(const pmr_pool_allocator_t &) noexcept =default;

        // A moved-from map can still own a sentinel allocated by this pool (MSVC).
        pmr_pool_allocator_t(pmr_pool_allocator_t &&other) noexcept: _resource { other._resource }
        {
        }

        pmr_pool_allocator_t &operator=(pmr_pool_allocator_t &&other) noexcept
        {
            _resource = other._resource;
            return *this;
        }

        template<typename U>
        pmr_pool_allocator_t(const pmr_pool_allocator_t<U> &other) noexcept: _resource { other._resource }
        {
        }

        [[nodiscard]] T *allocate(const size_t n)
        {
            if (n > std::numeric_limits<size_t>::max() / sizeof(T))
                throw std::bad_array_new_length {};
            return static_cast<T *>(_resource->allocate(n * sizeof(T), alignof(T)));
        }

        void deallocate(T *ptr, const size_t n) noexcept
        {
            _resource->deallocate(ptr, n * sizeof(T), alignof(T));
        }

        pmr_pool_allocator_t select_on_container_copy_construction() const
        {
            return fresh();
        }

        pmr_pool_allocator_t fresh() const
        {
            return pmr_pool_allocator_t { _resource->upstream_resource() };
        }

        template<typename U>
        bool operator==(const pmr_pool_allocator_t<U> &other) const noexcept
        {
            return _resource == other._resource;
        }

        friend void swap(pmr_pool_allocator_t &a, pmr_pool_allocator_t &b) noexcept
        {
            a._resource.swap(b._resource);
        }
    private:
        template<typename>
        friend struct pmr_pool_allocator_t;

        std::shared_ptr<std::pmr::unsynchronized_pool_resource> _resource;
    };
}
