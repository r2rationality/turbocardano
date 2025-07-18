#pragma once
/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include <map>
#include "mutex.hpp"
#include "format.hpp"
#include "logger.hpp"

namespace turbo {
    using progress_state = std::map<std::string, double>;
}

namespace fmt {
    template<>
    struct formatter<turbo::progress_state>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            auto out_it = ctx.out();
            for (auto it = v.begin(); it != v.end(); ++it) {
                const std::string_view sep { std::next(it) == v.end() ? "" : ", " };
                out_it = fmt::format_to(out_it, "{}: {:0.3f}%{}", it->first, it->second * 100, sep);
            }
            return out_it;
        }
    };
}

namespace turbo {
    struct progress {
        struct info {
            size_t total = 0;
            size_t active = 0;
            size_t completed = 0;
            size_t failed = 0;
        };

        static progress &get()
        {
            static progress p {}; // C++ standard guarantees a thread-safe initialization on the first call
            return p;
        }

        void init(const std::string &name)
        {
            _update(name, 0.0);
        }

        void update(const std::string &name, const uint64_t current, const uint64_t max)
        {
            const auto value = current < max ? current : max;
            const auto pct_value = max == 0 ? 1.0 : static_cast<double>(value) / max;
            _update(name, pct_value);
        }

        void update_inform(const std::string &name, const uint64_t current, const uint64_t max)
        {
            update(name, current, max);
            inform();
        }

        void done(const std::string &name)
        {
            _update(name, 1.0);
        }

        void retire(const std::string &name)
        {
            mutex::scoped_lock lk { _state_mutex };
            _state.erase(name);
        }

        void inform()
        {
            const auto now = std::chrono::system_clock::now();
            for (;;) {
                auto old_next = _next_inform.load(std::memory_order_relaxed);
                if (now < old_next)
                    break;
                auto new_next = now + std::chrono::milliseconds(1000);
                if (_next_inform.compare_exchange_weak(old_next, new_next, std::memory_order_acquire)) {
                    if (const auto state_copy = copy(); !state_copy.empty())
                        logger::info("progress: {}", state_copy);
                    break;
                }
            }
        }

        progress_state copy() const
        {
            progress_state state_copy {};
            {
                mutex::scoped_lock lk { _state_mutex };
                state_copy = _state;
            }
            return state_copy;
        }
    private:
        mutable mutex::unique_lock::mutex_type _state_mutex alignas(mutex::alignment) {};
        progress_state _state {};
        std::atomic<std::chrono::system_clock::time_point> _next_inform {};

        void _update(const std::string &name, const double value)
        {
            mutex::scoped_lock lk { _state_mutex };
            if (auto [it, created] = _state.try_emplace(name, value); !created && value > it->second)
                it->second = value;
        }
    };

    struct progress_guard {
        progress_guard(const std::initializer_list<std::string> &names): _names { names }, _progress { progress::get() }
        {
            for (const auto &name: _names)
                _progress.init(name);
        }

        ~progress_guard()
        {
            for (const auto &name: _names)
                _progress.retire(name);
        }

        std::span<const std::string> names() const
        {
            return _names;
        }
    private:
        const std::vector<std::string> _names;
        progress &_progress;
    };
}