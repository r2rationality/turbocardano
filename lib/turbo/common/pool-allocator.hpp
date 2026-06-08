#pragma once
/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com) */

#include <cstdint>
#include <cstring>
#include <forward_list>
#include <memory>
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

    template<typename T, size_t BATCH_SZ = 0x1000, bool SKIP_DTOR = std::is_trivially_destructible_v<T>, zero_policy_t ZERO_POLICY = zero_policy_t::none>
    struct pool_allocator_t {
        static_assert(SKIP_DTOR, "pool_allocator_t does not call T's destructor on pool destruction; "
            "set SKIP_DTOR=true to acknowledge this if T only owns resources within the same pool");
        static_assert(zero_policy_type_ok_t<T, ZERO_POLICY>::value,
            "pool_allocator_t zeroing is only supported for trivially copyable T");

        pool_allocator_t() = default;
        pool_allocator_t(const pool_allocator_t &) = delete;
        pool_allocator_t(pool_allocator_t &&) = delete;
        pool_allocator_t &operator=(const pool_allocator_t &) = delete;
        pool_allocator_t &operator=(pool_allocator_t &&) = delete;

        struct deleter_t {
            pool_allocator_t *_alloc = nullptr;

            void operator()(T* ptr) const
            {
                if (_alloc)
                    _alloc->deallocate(ptr);
            }
        };

        using ptr_t = std::unique_ptr<T, deleter_t>;

        T* allocate()
        {
            if (!_free.empty()) {
                auto ptr = _free.front();
                _free.pop_front();
                return ptr;
            }
            if (_arenas.empty() || _arena_offset == BATCH_SZ)
                _add_arena();
            std::byte* arena = static_cast<std::byte*>(_arenas.back());
            return reinterpret_cast<T*>(arena + (_arena_offset++) * sizeof(T));
        }

        void deallocate(T* ptr)
        {
            if (ptr) {
                if constexpr (zero_policy_has(ZERO_POLICY, zero_policy_t::free_list))
                    std::memset(ptr, 0, sizeof(T));
                _free.push_front(ptr);
            }
        }

        ~pool_allocator_t()
        {
            for (auto a: _arenas)
                operator delete(a, std::align_val_t{alignof(T)});
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
            return { raw, _deleter };
        }

        size_t free_count() const {
            return std::distance(_free.begin(), _free.end());
        }
    private:
        std::vector<void *> _arenas {};
        std::forward_list<T *> _free {};
        size_t _arena_offset = 0;
        deleter_t _deleter { this };

        void _add_arena()
        {
            auto *arena = operator new(BATCH_SZ * sizeof(T), std::align_val_t{alignof(T)});
            if constexpr (zero_policy_has(ZERO_POLICY, zero_policy_t::new_arena))
                std::memset(arena, 0, BATCH_SZ * sizeof(T));
            _arenas.push_back(arena);
            _arena_offset = 0;
        }
    };
}
