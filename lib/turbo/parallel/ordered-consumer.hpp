#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <atomic>
#include <turbo/common/error.hpp>
#include <turbo/common/logger.hpp>
#include <turbo/common/scheduler.hpp>

namespace turbo::parallel {
    struct ordered_consumer {
        using index_type = uint64_t;
        using consumer_func = std::function<void(index_type)>;

        ordered_consumer(consumer_func &&consumer, const std::string &name="ordered-consumer", const int64_t prio=0, scheduler &sched=scheduler::get()):
            _consumer { consumer }, _sched_name { name }, _sched_prio { prio }, _sched { sched }
        {
        }

        bool try_push(const index_type end_idx)
        {
            if (_error.load(std::memory_order_relaxed)) [[unlikely]]
                throw error("One of the consumer executions has raised an error. Impossible to proceed. Please consult logs for details.");
            if (end_idx > _next.load(std::memory_order_relaxed) && !_running.load(std::memory_order_relaxed)) {
                bool exp = false;
                if (_running.compare_exchange_strong(exp, true, std::memory_order_acquire, std::memory_order_relaxed)) {
                    const auto start_idx = _next.load(std::memory_order_acquire);
                    _sched.submit(
                        _sched_name,
                        _sched_prio,
                        [&, start_idx, end_idx] {
                            const auto ex_ptr = logger::run_log_errors([&] {
                                for (auto idx = start_idx; idx < end_idx; ++idx) {
                                    _consumer(idx);
                                    _next.store(idx + 1, std::memory_order_release);
                                }
                            }, [&] {
                                _running.store(false, std::memory_order_release);
                            });
                            if (ex_ptr)
                                _error.store(true, std::memory_order_release);
                        }
                    );
                    return true;
                }
            }
            return false;
        }

        bool cancel() const
        {
            return _error.load(std::memory_order_relaxed);
        }

        index_type next() const
        {
            return _next.load(std::memory_order_relaxed);
        }
    private:
        const consumer_func _consumer;
        const std::string _sched_name;
        const int64_t _sched_prio;
        scheduler &_sched;
        std::atomic<index_type> _next { 0 };
        std::atomic_bool _running { false };
        std::atomic_bool _error { false };
    };
}