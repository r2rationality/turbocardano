#pragma once
/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include <forward_list>
#include <memory>
#include <vector>

namespace turbo {
    template<typename T>
    struct pool_deleter_t;

    template<typename T, size_t BATCH_SZ = 0x1000>
    struct pool_allocator_t {
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
            if (ptr)
                _free.push_front(ptr);
        }

        ~pool_allocator_t()
        {
            for (auto a: _arenas)
                operator delete(a);
        }

        template<typename... Args>
        ptr_t make_ptr(Args&&... args)
        {
            T* raw = allocate();
            new (raw) T { std::forward<Args>(args)... };  // Placement new
            return { raw, _deleter };
        }
    private:
        std::vector<void *> _arenas {};
        std::forward_list<T *> _free {};
        size_t _arena_offset = 0;
        deleter_t _deleter { this };

        void _add_arena()
        {
            _arenas.push_back(operator new (BATCH_SZ * sizeof(T)));
            _arena_offset = 0;
        }
    };
}
