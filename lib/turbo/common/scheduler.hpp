#pragma once
/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include <any>
#include <chrono>
#include <functional>
#include <turbo/common/format.hpp>

namespace turbo {
    typedef fmt_error scheduler_error;

    struct scheduled_task {
        int64_t priority;
        std::string task_group;
        std::function<void()> task;
        std::optional<std::any> param {};

        bool operator<(const scheduled_task &t) const noexcept
        {
            return priority < t.priority;
        }
    };

    struct scheduled_task_error: scheduler_error {
        template<typename... Args>
        scheduled_task_error(const std::source_location &loc, scheduled_task &&task, const char *fmt, Args&&... a)
            : scheduler_error { loc, fmt, std::forward<Args>(a)... }, _task { std::move(task) }
        {
        }

        [[nodiscard]] const scheduled_task &task() const
        {
            return _task;
        }
    private:
        scheduled_task _task;
    };

    struct scheduled_result {
        int64_t priority = 0;
        std::string task_group {};
        std::any result {};
        double cpu_time = 0.0;

        bool operator<(const scheduled_result &r) const noexcept
        {
            return priority < r.priority;
        }
    };

    struct scheduler {
        using task_func_t = std::function<void()>;
        using submit_func_t = std::function<void(scheduled_task)>;
        using todo_count_t = std::shared_ptr<std::atomic_size_t>;
        using wait_all_submit_func_t = std::function<void(const todo_count_t &, const submit_func_t &)>;
        using cancel_predicate_t = std::function<bool(const std::string &, const std::optional<std::any> &param)>;
        using error_observer_t = std::function<void(const scheduled_task_error &)>;

        static constexpr std::chrono::milliseconds default_wait_interval { 10 };
        static constexpr std::chrono::milliseconds default_update_interval { 5000 };

        static size_t default_worker_count()
        {
            return std::thread::hardware_concurrency();
        }

        static scheduler &get()
        {
            static scheduler sched {};
            return sched;
        }

        explicit scheduler(size_t user_num_workers=default_worker_count());
        ~scheduler();
        [[nodiscard]] size_t num_workers() const;
        size_t cancel(const cancel_predicate_t &pred);
        void submit(std::string name, int64_t priority, const task_func_t &task, std::optional<std::any> param=std::nullopt);
        void on_error(const std::string &task_group, const error_observer_t &observer, bool replace=false);
        [[nodiscard]] bool process_ok(bool report_status=true, const std::source_location &loc=std::source_location::current());
        void process(bool report_status=true, const std::source_location &loc=std::source_location::current());
        void process_once(bool report_statues=true);
        void wait_all(const std::string &task_group, const wait_all_submit_func_t &submit_func);
    private:
        struct impl;
        std::unique_ptr<impl> _impl;
    };
}