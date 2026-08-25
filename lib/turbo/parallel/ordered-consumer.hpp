#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

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
            _throw_if_error();
            auto requested_end = _requested_end.load(std::memory_order_relaxed);
            while (requested_end < end_idx && !_requested_end.compare_exchange_weak(
                    requested_end, end_idx, std::memory_order_release, std::memory_order_relaxed)) {
            }
            return _try_start();
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
        std::atomic<index_type> _requested_end { 0 };
        std::atomic_bool _running { false };
        std::atomic_bool _error { false };

        bool _try_start()
        {
            if (_requested_end.load(std::memory_order_acquire) <= _next.load(std::memory_order_relaxed)
                    || _running.load(std::memory_order_relaxed)) {
                return false;
            }
            bool exp = false;
            if (!_running.compare_exchange_strong(exp, true, std::memory_order_acquire, std::memory_order_relaxed))
                return false;
            try {
                // A failed consumer publishes the error before releasing
                // _running. Recheck after acquiring that handoff.
                _throw_if_error();
            } catch (...) {
                _running.store(false, std::memory_order_release);
                throw;
            }
            _sched.submit(
                _sched_name,
                _sched_prio,
                [this] {
                    const auto ex_ptr = logger::run_log_errors([&] {
                        for (;;) {
                            const auto idx = _next.load(std::memory_order_relaxed);
                            if (idx >= _requested_end.load(std::memory_order_acquire))
                                break;
                            _consumer(idx);
                            _next.store(idx + 1, std::memory_order_release);
                        }
                    });
                    if (ex_ptr)
                        _error.store(true, std::memory_order_release);
                    // Publish the final progress or error state before making
                    // the consumer available to another producer.
                    _running.store(false, std::memory_order_release);
                    // A producer may have extended the requested range between
                    // the final range check and the _running handoff.
                    if (!ex_ptr)
                        _try_start();
                }
            );
            return true;
        }

        void _throw_if_error() const
        {
            if (_error.load(std::memory_order_acquire)) [[unlikely]]
                throw error("One of the consumer executions has raised an error. Impossible to proceed. Please consult logs for details.");
        }
    };
}
