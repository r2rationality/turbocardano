#pragma once
/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include <atomic>
#include <coroutine>
#include <exception>
#include <future>
#include <mutex>
#include <optional>
#include <utility>
#include "error.hpp"
#include "scheduler.hpp"

namespace turbo::coro {
    template<typename T>
    struct generator_t {
        struct promise_type;
        using handle_type = std::coroutine_handle<promise_type>;

        struct promise_type {
            generator_t get_return_object()
            {
                return generator_t { handle_type::from_promise(*this) };
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
                _current_value = std::move(value);
                return {};
            }

            void return_void()
            {
            }

            void unhandled_exception()
            {
                std::terminate();
            }
        private:
            friend generator_t;
            std::optional<T> _current_value {};
        };

        generator_t(generator_t&& t) noexcept:
            _coro { std::exchange(t._coro, {}) }
        {
        }

        generator_t& operator=(generator_t&& t) noexcept
        {
            if (this != &t) [[likely]] {
                if (_coro) [[likely]]
                    _coro.destroy();
                _coro = std::exchange(t._coro, {});
            }
            return *this;
        }

        ~generator_t()
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

        T result()
        {
            if (!_coro) [[unlikely]]
                throw error("an attempt to call take on an empty coro::generator_t!");
            auto &val = _coro.promise()._current_value;
            if (!val) [[unlikely]]
                throw error("an attempt to take from an empty promise!");
            auto res = std::move(*val);
            val.reset();
            return res;
        }
    private:
        handle_type _coro;

        explicit generator_t(handle_type h):
            _coro { h }
        {
        }
    };

    template <typename T>
    struct result_storage_t {
        void set_value(T&& v) { _value = std::move(v); }
        T get() {
            if (!_value) [[unlikely]]
                throw error("get called on a coroutine result storage before it was set!");
            return std::move(*_value);
        }
    protected:
        std::optional<T> _value;
    };

    template <>
    struct result_storage_t<void> {
        void set_value() {}
        void get() {}
    };

    template <typename T>
    struct promise_base_t : result_storage_t<T> {
        void return_value(T v) { this->set_value(std::move(v)); }
    };

    template <>
    struct promise_base_t<void> : result_storage_t<void> {
        void return_void() { this->set_value(); }
    };

    template<typename T>
    struct task_t {
        struct promise_type;
        using handle_type = std::coroutine_handle<promise_type>;

        struct promise_type: promise_base_t<T>{
            task_t get_return_object()
            {
                return task_t{_my_handle()};
            }

            std::suspend_always initial_suspend() noexcept
            {
                return  {};
            }

            std::suspend_always final_suspend() noexcept
            {
                auto ch = _caller;
                scheduler::get().submit("final-suspend", 100, [ch] {
                    if (ch && !ch.done())
                        ch.resume();
                });
                return {};
            }

            void unhandled_exception()
            {
                _exception = std::current_exception();
            }

            void set_exception(std::exception_ptr e) noexcept
            {
                _exception = e;
            }
        private:
            handle_type _my_handle()
            {
                return handle_type::from_promise(*this);
            }

            friend task_t;
            std::exception_ptr _exception {};
            std::coroutine_handle<> _caller {};
        };

        task_t(task_t &&o) noexcept:
            _coro{std::exchange(o._coro, {})}
        {
        }

        task_t& operator=(task_t &&o) noexcept
        {
            if (this != &o) {
                if (_coro)
                    _coro.destroy();
                _coro = std::exchange(o._coro, {});
            }
            return *this;
        }

        ~task_t()
        {
            if (_coro)
                _coro.destroy();
        }

        bool await_ready() const noexcept
        {
            return done();
        }

        void await_suspend(std::coroutine_handle<> h)
        {
            _coro.promise()._caller = h;
            resume();
        }

        T await_resume() noexcept
        {
            return result();
        }

        [[nodiscard]] bool done() const
        {
            return _coro && _coro.done();
        }

        void resume()
        {
            if (!_coro.done())
                _coro.resume();
        }

        T result()
        {
            auto &p = _coro.promise();
            if (p._exception)
                std::rethrow_exception(p._exception);
            return p.get();
        }

        T wait()
        {
            std::promise<T> promise;
            auto future = promise.get_future();
            auto wrapped_task = _notify_future(std::move(promise), *this);
            wrapped_task.resume();
            future.wait();
            return future.get();
        }
    private:
        task_t<void> _notify_future(std::promise<T> promise, task_t<T> &task)
        {
            auto res = co_await task;
            promise.set_value(std::move(res));
        }

        explicit task_t(handle_type h):
            _coro{h}
        {
        }

        handle_type _coro;
    };

    struct external_task_t {
        using action_t = std::function<void(std::coroutine_handle<>)>;

        external_task_t(action_t suspend_action):
            _suspend_action{std::move(suspend_action)}
        {
        }

        bool await_ready() const noexcept
        {
            return false;
        }

        void await_suspend(std::coroutine_handle<> h)
        {
            _suspend_action.operator()(h);
        }

        void await_resume() noexcept
        {
        }
    private:
        action_t _suspend_action;
    };

    struct get_handle_t {
        using action_t = std::function<void(std::coroutine_handle<>)>;

        get_handle_t(action_t suspend_action):
            _suspend_action{std::move(suspend_action)}
        {
        }

        bool await_ready() const noexcept
        {
            return false;
        }

        void await_suspend(std::coroutine_handle<> h)
        {
            _suspend_action.operator()(h);
            h.resume();
        }

        void await_resume() noexcept
        {
        }
    private:
        action_t _suspend_action;
    };
}
