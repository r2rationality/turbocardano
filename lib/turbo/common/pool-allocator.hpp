#pragma once
/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com) */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace turbo {
    enum class zero_policy_t: uint8_t {
        none = 0,
        new_arena = 1,
        free_list = 2,
        all = new_arena | free_list
    };

    template<typename T, zero_policy_t ZERO_POLICY>
    struct zero_policy_type_ok_t: std::true_type {};

    template<typename T>
    struct zero_policy_type_ok_t<T, zero_policy_t::new_arena>: std::bool_constant<std::is_trivially_copyable_v<T>> {};

    template<typename T>
    struct zero_policy_type_ok_t<T, zero_policy_t::free_list>: std::bool_constant<std::is_trivially_copyable_v<T>> {};

    template<typename T>
    struct zero_policy_type_ok_t<T, zero_policy_t::all>: std::bool_constant<std::is_trivially_copyable_v<T>> {};

    [[nodiscard]] constexpr bool zero_policy_has(const zero_policy_t policy, const zero_policy_t flag) noexcept
    {
        return (static_cast<uint8_t>(policy) & static_cast<uint8_t>(flag)) != 0;
    }

    namespace detail {
        template<size_t BATCH_SZ, zero_policy_t ZERO_POLICY>
        struct pool_allocator_resource_t {
            static_assert(BATCH_SZ > 0, "a pool allocator batch must contain at least one slot");

            pool_allocator_resource_t() =default;
            pool_allocator_resource_t(const pool_allocator_resource_t &) = delete;
            pool_allocator_resource_t(pool_allocator_resource_t &&) = delete;
            pool_allocator_resource_t &operator=(const pool_allocator_resource_t &) = delete;
            pool_allocator_resource_t &operator=(pool_allocator_resource_t &&) = delete;

            void *allocate(const size_t size, const size_t alignment)
            {
                if (_bulk_release)
                    std::terminate();
                auto &bucket = _bucket(size, alignment);
                if (bucket.free) {
                    auto *ptr = bucket.free;
                    std::memcpy(&bucket.free, ptr, sizeof(bucket.free));
                    --bucket.free_count;
                    if constexpr (zero_policy_has(ZERO_POLICY, zero_policy_t::free_list))
                        std::memset(ptr, 0, size);
                    return ptr;
                }
                if (bucket.arenas.empty() || bucket.arena_offset == bucket.arena_capacity)
                    _add_arena(bucket);
                auto *arena = static_cast<std::byte *>(bucket.arenas.back());
                return arena + bucket.arena_offset++ * bucket.stride;
            }

            void deallocate(void *ptr, const size_t size, const size_t alignment) noexcept
            {
                if (!ptr)
                    return;
                if (_bulk_release)
                    return;
                auto *bucket = _find_bucket(size, alignment);
                if (!bucket)
                    std::terminate();
                std::memcpy(ptr, &bucket->free, sizeof(bucket->free));
                bucket->free = ptr;
                ++bucket->free_count;
            }

            [[nodiscard]] size_t free_count() const noexcept
            {
                size_t count = 0;
                for (const auto &bucket: _buckets)
                    count += bucket->free_count;
                return count;
            }

            void begin_bulk_release() noexcept
            {
                _bulk_release = true;
            }

        private:
            struct bucket_t {
                size_t size;
                size_t alignment;
                size_t stride;
                std::vector<void *> arenas {};
                void *free = nullptr;
                size_t free_count = 0;
                size_t arena_offset = 0;
                size_t arena_capacity = 0;
                size_t next_arena_capacity = 1;

                bucket_t(const size_t item_size, const size_t item_alignment):
                    size { item_size },
                    alignment { std::max(item_alignment, alignof(void *)) },
                    stride { _align_up(std::max(item_size, sizeof(void *)), alignment) }
                {
                }

                ~bucket_t()
                {
                    for (auto *arena: arenas)
                        operator delete(arena, std::align_val_t { alignment });
                }

            private:
                static size_t _align_up(const size_t size, const size_t alignment)
                {
                    const auto rem = size % alignment;
                    if (!rem)
                        return size;
                    const auto padding = alignment - rem;
                    if (size > std::numeric_limits<size_t>::max() - padding)
                        throw std::bad_array_new_length {};
                    return size + padding;
                }
            };

            std::vector<std::unique_ptr<bucket_t>> _buckets {};
            bool _bulk_release = false;

            bucket_t *_find_bucket(const size_t size, const size_t alignment) noexcept
            {
                for (auto &bucket: _buckets) {
                    if (bucket->size == size && bucket->alignment == std::max(alignment, alignof(void *)))
                        return bucket.get();
                }
                return nullptr;
            }

            bucket_t &_bucket(const size_t size, const size_t alignment)
            {
                if (auto *bucket = _find_bucket(size, alignment))
                    return *bucket;
                return *_buckets.emplace_back(std::make_unique<bucket_t>(size, alignment));
            }

            static void _add_arena(bucket_t &bucket)
            {
                const auto capacity = bucket.next_arena_capacity;
                if (bucket.stride > std::numeric_limits<size_t>::max() / capacity)
                    throw std::bad_array_new_length {};
                const auto arena_size = capacity * bucket.stride;
                auto *arena = operator new(arena_size, std::align_val_t { bucket.alignment });
                if constexpr (zero_policy_has(ZERO_POLICY, zero_policy_t::new_arena))
                    std::memset(arena, 0, arena_size);
                try {
                    bucket.arenas.push_back(arena);
                } catch (...) {
                    operator delete(arena, std::align_val_t { bucket.alignment });
                    throw;
                }
                bucket.arena_offset = 0;
                bucket.arena_capacity = capacity;
                bucket.next_arena_capacity = capacity >= (BATCH_SZ + 1) / 2
                    ? BATCH_SZ
                    : capacity * 2;
            }
        };
    }

    // A standard-library-compatible recyclable pool allocator. Allocator copies
    // and rebinds share a resource, which is required by node-based containers.
    // A copied container receives a fresh resource so that one container can
    // never release storage still owned by another container.
    template<typename T, size_t BATCH_SZ = 0x1000, bool SKIP_DTOR = std::is_trivially_destructible_v<T>, zero_policy_t ZERO_POLICY = zero_policy_t::none>
    struct pool_allocator_t {
        static_assert(SKIP_DTOR, "pool_allocator_t does not call T's destructor on pool destruction; "
            "set SKIP_DTOR=true to acknowledge this if T only owns resources within the same pool");
        static_assert(zero_policy_type_ok_t<T, ZERO_POLICY>::value,
            "pool_allocator_t zeroing is only supported for trivially copyable T");

        using value_type = T;
        using size_type = size_t;
        using difference_type = std::ptrdiff_t;
        using propagate_on_container_move_assignment = std::true_type;
        using propagate_on_container_swap = std::true_type;
        using is_always_equal = std::false_type;

        template<typename U>
        struct rebind {
            using other = pool_allocator_t<U, BATCH_SZ, SKIP_DTOR, ZERO_POLICY>;
        };

        pool_allocator_t(): _resource { std::make_shared<resource_type>() }
        {
        }

        pool_allocator_t(const pool_allocator_t &) noexcept =default;

        // Moving a standard container's allocator must leave the source able
        // to release storage that the container implementation leaves behind.
        // In particular, MSVC's node containers allocate a replacement
        // sentinel before propagating the allocator during move assignment.
        // Sharing the resource is cheap and keeps both allocator objects valid.
        pool_allocator_t(pool_allocator_t &&o) noexcept:
            _resource { o._resource }
        {
        }

        pool_allocator_t &operator=(const pool_allocator_t &) noexcept =default;

        pool_allocator_t &operator=(pool_allocator_t &&o) noexcept
        {
            _resource = o._resource;
            return *this;
        }

        template<typename U>
        pool_allocator_t(const pool_allocator_t<U, BATCH_SZ, SKIP_DTOR, ZERO_POLICY> &o) noexcept:
            _resource { o._resource }
        {
        }

        struct deleter_t {
            std::shared_ptr<detail::pool_allocator_resource_t<BATCH_SZ, ZERO_POLICY>> resource {};

            void operator()(T *ptr) const noexcept
            {
                if (resource)
                    resource->deallocate(ptr, sizeof(T), alignof(T));
            }
        };

        using ptr_t = std::unique_ptr<T, deleter_t>;

        [[nodiscard]] T *allocate(const size_t n=1)
        {
            if (n > std::numeric_limits<size_t>::max() / sizeof(T))
                throw std::bad_array_new_length {};
            return static_cast<T *>(_get_resource().allocate(n * sizeof(T), alignof(T)));
        }

        void deallocate(T *ptr, const size_t n=1) noexcept
        {
            if (!ptr)
                return;
            if (!_resource || n > std::numeric_limits<size_t>::max() / sizeof(T))
                std::terminate();
            _resource->deallocate(ptr, n * sizeof(T), alignof(T));
        }

        [[nodiscard]] pool_allocator_t select_on_container_copy_construction() const
        {
            return fresh();
        }

        [[nodiscard]] pool_allocator_t fresh() const
        {
            return {};
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
            return { raw, deleter_t { _resource } };
        }

        [[nodiscard]] size_t free_count() const noexcept
        {
            return _resource ? _resource->free_count() : 0;
        }

        // The allocator and every allocation backed by it must be destroyed
        // immediately after this call. Deallocation is suppressed so that the
        // resource destructor can drop all arena pages in bulk.
        void begin_bulk_release() noexcept
        {
            if (_resource)
                _resource->begin_bulk_release();
        }

        template<typename U>
        [[nodiscard]] bool operator==(const pool_allocator_t<U, BATCH_SZ, SKIP_DTOR, ZERO_POLICY> &o) const noexcept
        {
            return _resource == o._resource;
        }

    private:
        template<typename, size_t, bool, zero_policy_t>
        friend struct pool_allocator_t;

        using resource_type = detail::pool_allocator_resource_t<BATCH_SZ, ZERO_POLICY>;
        std::shared_ptr<resource_type> _resource {};

        resource_type &_get_resource()
        {
            if (!_resource)
                _resource = std::make_shared<resource_type>();
            return *_resource;
        }
    };
}
