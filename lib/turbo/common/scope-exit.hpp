#pragma once
/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include <utility>
#include <exception>

namespace turbo {
    template<typename F>
    struct scope_exit {
        scope_exit(const scope_exit &) = delete;
        scope_exit& operator=(const scope_exit &) = delete;
        scope_exit& operator=(scope_exit &&) = delete;

        explicit scope_exit(F f) noexcept(std::is_nothrow_move_constructible_v<F>):
            _func{std::move(f)}
        {
        }

        scope_exit(scope_exit &&o) noexcept(std::is_nothrow_move_constructible_v<F>):
            _func{std::move(o._func)},
            _active{o._active}
        {
            o.release();
        }

        ~scope_exit() noexcept
        {
            if (_active)
                _func();
        }

        void release() noexcept
        {
            _active = false;
        }
    private:
        F _func;
        bool _active = true;
    };

    template<typename F>
    scope_exit<F> make_scope_exit(F f)
    {
        return scope_exit<F>(std::move<F>(f));
    }
}