#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <coroutine>
#include <vector>
#include <exception>
#include <utility>

namespace daedalus_turbo::coro {
    template<typename T>
    struct generator_task_t {
        struct promise_type;
        using handle_type = std::coroutine_handle<promise_type>;

        struct promise_type {
            std::optional<T> current_value {};

            generator_task_t get_return_object()
            {
                return generator_task_t { handle_type::from_promise(*this) };
            }

            std::suspend_always initial_suspend() noexcept
            {
                return {};
            }

            std::suspend_always final_suspend() noexcept
            {
                return {};
            }

            std::suspend_always yield_value(T value)
            {
                current_value = std::move(value);
                return {};
            }

            void return_void()
            {
            }

            void unhandled_exception()
            {
                std::terminate();
            }
        };

        generator_task_t(generator_task_t&& t) noexcept:
            _coro { std::exchange(t._coro, {}) }
        {
        }

        generator_task_t& operator=(generator_task_t&& t) noexcept
        {
            if (this != &t) [[likely]] {
                if (_coro) [[likely]]
                    _coro.destroy();
                _coro = std::exchange(t._coro, {});
            }
            return *this;
        }

        ~generator_task_t()
        {
            if (_coro) [[likely]]
                _coro.destroy();
        }

        bool resume()
        {
            if (!_coro || _coro.done())
                return false;
            _coro.resume();
            return !_coro.done();
        }

        T take()
        {
            auto &val = _coro.promise().current_value;
            if (!val) [[unlikely]]
                throw error("an attempt to take from an empty promise!");
            auto res = std::move(*val);
            val.reset();
            return res;
        }
    private:
        handle_type _coro;

        explicit generator_task_t(handle_type h):
            _coro { h }
        {
        }
    };
}