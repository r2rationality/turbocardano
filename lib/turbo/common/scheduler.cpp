/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <list>
#include <queue>
#include <source_location>
#include <unordered_map>
#include <vector>

#include <boost/container/flat_map.hpp>
#include <boost/thread.hpp>

#include "logger.hpp"
#include "memory.hpp"
#include "mutex.hpp"
#include "progress.hpp"
#include "scheduler.hpp"
#include "timer.hpp"

namespace fmt {
    template<>
    struct formatter<boost::thread::id>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out())
        {
            std::ostringstream ss {};
            ss << v;
            return fmt::format_to(ctx.out(), "{}", ss.str());
        }
    };
}

namespace turbo {
    struct scheduler::impl {
        explicit impl(const size_t user_num_workers)
            : _num_workers { _find_num_workers(user_num_workers) }
        {
            if (_num_workers == 0)
                throw error("the number of worker threads must be greater than zero!");
            logger::info("scheduler started, worker count: {}", _num_workers);
            _worker_tasks.resize(_num_workers);
            _worker_ids.reserve(_num_workers);
            // One worker is a special case handled by the process method itself
            if (_num_workers == 1) {
                _worker_ids.emplace(boost::this_thread::get_id(), 0);
            } else {
                boost::thread::attributes attrs {};
                attrs.set_stack_size(16 << 20);
                for (size_t i = 0; i < _num_workers; ++i) {
                    _workers.emplace_back(attrs, [this, i]() { _worker_thread(i); });
                    _worker_ids.emplace_hint(_worker_ids.end(), _workers.back().get_id(), i);
                }
            }
        }

        ~impl()
        {
            _destroy = true;
            _tasks_cv.notify_all();
            for (auto &w: _workers)
                w.join();
            _workers.clear();

            logger::debug("scheduler's peak RAM use: {} MB", memory::max_usage_mb());
            logger::debug("scheduler's cumulative cpu utilization statistics by task group:");
            task_stats_map grouped_stats {};
            double total_cpu_time = 0;
            for (const auto &[task_name, stats]: _task_stats) {
                const auto pos = task_name.find(':');
                auto [it, created] = grouped_stats.try_emplace(std::string { pos == task_name.npos ? task_name : task_name.substr(0, pos) }, stats);
                if (!created) {
                    it->second.submitted += stats.submitted;
                    it->second.completed += stats.completed;
                    it->second.cpu_time += stats.cpu_time;
                }
                total_cpu_time += stats.cpu_time;
            }
            std::vector<std::pair<std::string, task_stat>> sorted_stats {};
            std::copy(grouped_stats.begin(), grouped_stats.end(), std::back_inserter(sorted_stats));
            std::sort(sorted_stats.begin(), sorted_stats.end(), [](const auto &a, const auto &b) { return a.second.cpu_time > b.second.cpu_time; });
            for (const auto &[task_name, stats]: sorted_stats) {
                logger::debug("task: {} submitted: {} completed: {} cpu_time: {:0.3f} sec ({:0.1f}%)",
                    task_name, stats.submitted, stats.completed, stats.cpu_time, 100 * stats.cpu_time / total_cpu_time);
            }
            logger::debug("total cpu time spent by all tasks: {:0.3f} sec", total_cpu_time);
        }

        size_t num_workers() const
        {
            return _num_workers;
        }

        size_t cancel(const cancel_predicate_t &pred)
        {
            size_t num_cancelled = 0;
            mutex::scoped_lock tasks_lock { _tasks_mutex };
            task_queue new_tasks {};
            while (!_tasks.empty()) {
                auto task = _tasks.top();
                _tasks.pop();
                if (pred(task.task_group, task.param)) {
                    --_task_stats[task.task_group].queued;
                    ++num_cancelled;
                } else {
                    new_tasks.emplace(std::move(task));
                }
            }
            _tasks = std::move(new_tasks);
            // no need to notify since cancel does not add new tasks
            return num_cancelled;
        }

        void post(scheduled_task task)
        {
            mutex::unique_lock tasks_lock { _tasks_mutex };
            auto [ it, created ] = _task_stats.emplace(task.task_group, task_stat { 1, 1 });
            if (!created) {
                ++it->second.submitted;
                ++it->second.queued;
            }
            _tasks.emplace(std::move(task));
            tasks_lock.unlock();
            _tasks_cv.notify_one();
        }

        void on_error(const std::string &task_group, const error_observer_t &obs, const bool replace)
        {
            if (task_count(task_group) != 0) [[unlikely]]
                throw error(fmt::format("observers for task '{}' must be configured before task submission!", task_group));
            const mutex::scoped_lock lock { _observers_mutex };
            auto [it, created] = _observers.emplace(task_group, obs);
            if (!created) {
                if (!replace) [[unlikely]]
                    throw error(fmt::format("task {}: on_error observer has already been set!", task_group));
                it->second = obs;
            }
        }

        size_t task_count(const std::string &task_group)
        {
            size_t cnt = 0;
            {
                mutex::scoped_lock lock { _tasks_mutex };
                if (const auto it = _task_stats.find(task_group); it != _task_stats.end())
                    cnt = it->second.queued;
            }
            return cnt;
        }

        bool process_ok(const bool report_status=true, const std::source_location &loc=std::source_location::current())
        {
            timer t { fmt::format("scheduler::process_ok call from {}:{}", loc.file_name(), loc.line()), logger::level::debug, true };
            bool must_be_false = false;
            if (!_process_running.compare_exchange_strong(must_be_false, true)) [[unlikely]]
                throw error("nested calls to scheduler::process are prohibited!");
            const auto finalize = [&] {
                {
                    mutex::scoped_lock observers_lock { _observers_mutex };
                    _observers.clear();
                }
                _process_running = false;
                _success = true;
            };
            try {
                _process(report_status);
                const bool res = _success.load();
                finalize();
                return res;
            } catch (const std::exception &ex) {
                logger::warn("scheduler::process failed: {}", ex.what());
                finalize();
                throw;
            }
        }

        void process(const bool report_status=true, const std::source_location &loc=std::source_location::current())
        {
            if (!process_ok(report_status, loc)) [[unlikely]]
                throw error("some scheduled tasks have failed, please consult logs for more details");
        }

        void process_once(const bool report_status=true)
        {
            // to ensure that result observers are always called from one thread
            // process once will process results only if there is no _process running
            _process_once(report_status, false, !_process_running);
        }

        void wait_all_done(const std::string &task_group, const wait_all_submit_func_t &submit_func)
        {
            bool exp_false = false;
            if (!_wait_all_done_running.compare_exchange_strong(exp_false, true))
                throw error("concurrent wait_all_done calls are not allowed!");
            if (_num_workers < 4)
                throw error(fmt::format("wait_all_done relies on a high worker count but got {} worker threads!", _num_workers));
            std::atomic_size_t errors = 0;
            try {
                static constexpr std::chrono::milliseconds report_period{10000};
                const auto wait_start = std::chrono::system_clock::now();
                auto next_warn = wait_start + report_period;
                on_error(task_group, [&errors](const auto &) {
                    errors.fetch_add(1, std::memory_order_relaxed);
;                }, true);
                auto todo = std::make_shared<std::atomic<size_t>>(0);
                submit_func(todo, [&](auto task) {
                    todo->fetch_add(1, std::memory_order_relaxed);
                    task.task = [prev_action=task.task, todo] {
                        prev_action();
                        todo->fetch_sub(1, std::memory_order_relaxed);
                    };
                    post(std::move(task));
                });
                const auto process_results = !_process_running.load();
                for (;;) {
                    auto num_todo = todo->load(std::memory_order_relaxed) - errors.load(std::memory_order_relaxed);
                    if (num_todo == 0)
                        break;
                    if (const auto now = std::chrono::system_clock::now(); now >= next_warn) {
                        next_warn = now + report_period;
                        logger::warn(
                            "wait_all_done takes longer than expected task: {} todo: {} errors: {} process_results: {} waiting for: {} secs",
                            task_group, num_todo, errors.load(), process_results,
                            std::chrono::duration_cast<std::chrono::seconds>(now - wait_start).count());
                    }
                    _process_once(true, false, process_results);
                }
                _wait_all_done_running = false;
            } catch (const std::exception &ex) {
                logger::warn("wait_all_done failed with std::exception: {}", ex.what());
                _wait_all_done_running = false;
                throw;
            } catch (...) {
                logger::warn("wait_all_done failed with an unknown exception");
                _wait_all_done_running = false;
                throw;
            }
            if (errors > 0) [[unlikely]]
                throw scheduler_error(fmt::format("wait_all_done {} - there were failed tasks; cannot continue", task_group));
        }
    private:
        struct completion_action {
            std::function<void()> action {};
            size_t todo = 0;
            size_t done = 0;
        };
        using task_queue = std::priority_queue<scheduled_task>;

        struct task_stat {
            size_t submitted = 0;
            size_t queued = 0;
            size_t completed = 0;
            double cpu_time = 0.0;
        };
        using task_stats_map = std::unordered_map<std::string, task_stat>;

        mutable mutex::unique_lock::mutex_type _tasks_mutex alignas(mutex::alignment) {};
        std::condition_variable_any _tasks_cv alignas(mutex::alignment) {};
        task_queue _tasks {};
        task_stats_map _task_stats {};

        using observer_map = std::unordered_map<std::string, error_observer_t>;
        mutable mutex::unique_lock::mutex_type _observers_mutex alignas(mutex::alignment) {};
        observer_map _observers {};

        std::vector<boost::thread> _workers {};
        boost::container::flat_map<boost::thread::id, size_t> _worker_ids {};
        std::vector<std::optional<std::string>> _worker_tasks {};
        const size_t _num_workers;
        std::atomic_size_t _num_active = 0;
        std::atomic_bool _destroy { false };
        std::atomic_bool _success { true };
        std::atomic_bool _process_running { false };
        std::atomic_bool _wait_all_done_running { false };
        std::atomic<std::chrono::time_point<std::chrono::system_clock>> _report_next_time { std::chrono::system_clock::now() + default_update_interval };

        static size_t _find_num_workers(size_t user_num_workers)
        {
            const char *env_workers_str = std::getenv("DT_WORKERS");
            if (env_workers_str != nullptr) {
                size_t env_workers = std::stoul(env_workers_str);
                if (env_workers != 0)
                    return env_workers;
            }
            return user_num_workers;
        }

        std::optional<size_t> _get_worker_id() const
        {
            const auto w_it = _worker_ids.find(boost::this_thread::get_id());
            if (w_it != _worker_ids.end())
                return w_it->second;
            return {};
        }

        void _report_status()
        {
            const auto now = std::chrono::system_clock::now();
            auto prev_next_time = _report_next_time.load();
            if (now >= prev_next_time) {
                const auto next_next_time = now + default_update_interval;
                if (_report_next_time.compare_exchange_strong(prev_next_time, next_next_time)) {
                    size_t num_tasks = 0;
                    std::map<std::string, size_t> active_tasks {};
                    {
                        mutex::scoped_lock tasks_lk { _tasks_mutex };
                        for (const auto &[task_name, stats]: _task_stats)
                            num_tasks += stats.queued;
                        for (const auto &task_name: _worker_tasks) {
                            if (task_name)
                                ++active_tasks[*task_name];
                        }
                    }
                    logger::debug("scheduler tasks total: {} active: {}", num_tasks, active_tasks);
                    progress::get().inform();
                }
            }
        }

        bool _worker_try_execute(size_t worker_idx, const std::optional<std::chrono::milliseconds> wait_interval_ms)
        {
            static std::string wait_task_name { "__WAIT_FOR_TASKS__" };
            const auto sleep_start_time = std::chrono::system_clock::now();
            mutex::unique_lock lock { _tasks_mutex };
            _tasks_cv.wait_for(lock, *wait_interval_ms, [&] {
                return !_tasks.empty() || _destroy;
            });
            _task_stats[wait_task_name].cpu_time += std::chrono::duration<double> { std::chrono::system_clock::now() - sleep_start_time }.count();
            if (_destroy) [[unlikely]]
                return false;
            if (!_tasks.empty()) {
                auto &worker_task = _worker_tasks[worker_idx];
                const auto prev_task = worker_task;
                if (!prev_task)
                    ++_num_active;
                // need to create copies since the task will be destroyed before reporting its result.
                std::optional<scheduled_task_error> task_err {};
                std::string task_group {};
                const auto start_time = std::chrono::system_clock::now();
                // ensure that the task instance is destroyed before its results are reported
                {
                    auto task = _tasks.top();
                    _tasks.pop();
                    task_group = task.task_group;
                    if (prev_task)
                        worker_task = fmt::format("{}/{}", *prev_task, task.task_group);
                    else
                        worker_task = task.task_group;
                    lock.unlock();
                    try {
                        task.task();
                    } catch (const std::exception &ex) {
                        _success = false;
                        logger::warn("worker-{} task {} std::exception: {}", worker_idx, task.task_group, ex.what());
                        task_err.emplace(std::source_location::current(), std::move(task), "task: '{}' error: '{}' of type: '{}'!", task.task_group, ex.what(), typeid(ex).name());
                    } catch (...) {
                        _success = false;
                        logger::warn("worker-{} task {} unknown exception", worker_idx, task.task_group);
                        task_err.emplace(std::source_location::current(), std::move(task), "task: '{}' unknown exception", task.task_group);
                    }
                }
                const auto cpu_time = std::chrono::duration<double> { std::chrono::system_clock::now() - start_time }.count();
                {
                    mutex::scoped_lock tasks_lock { _tasks_mutex };
                    if (auto it = _task_stats.find(task_group); it != _task_stats.end()) [[unlikely]] {
                        --it->second.queued;
                        ++it->second.completed;
                        it->second.cpu_time += cpu_time;
                    } else {
                        logger::error("internal error: unknown task: {}", task_group);
                    }
                }
                if (task_err) [[unlikely]] {
                    std::scoped_lock o_lk { _observers_mutex };
                    if (auto it = _observers.find(task_group); it != _observers.end()) {
                        logger::run_log_errors([&] {
                            it->second(*task_err);
                        });
                    }
                }
                lock.lock();
                worker_task = prev_task;
                lock.unlock();
                if (!prev_task)
                    --_num_active;
            }
            return true;
        }

        void _worker_thread(size_t worker_idx)
        {
            static auto wait_ms = default_wait_interval;
            while (_worker_try_execute(worker_idx, wait_ms)) {
            }
        }

        void _process_once(const bool report_status, const bool process_tasks=false, const bool /*process_results*/=false)
        {
            // In the single-worker mode, the tasks are executed in the loop
            if (process_tasks) {
                if (const auto w_id = _get_worker_id(); w_id)
                    _worker_try_execute(*w_id, default_wait_interval);
                else
                    logger::warn("Thread {} outside of the worker pool attempted to execute tasks", boost::this_thread::get_id());
            }
            if (report_status)
                _report_status();
        }

        void _process(const bool report_status)
        {
            for (;;) {
                {
                    mutex::scoped_lock tasks_lk { _tasks_mutex };
                    size_t num_tasks = 0;
                    for (const auto &[task_name, stats]: _task_stats)
                        num_tasks += stats.queued;
                    if (num_tasks == 0 && _num_active.load(std::memory_order_relaxed) == 0)
                        break;
                }
                _process_once(report_status, _num_workers == 1, true);
            }
            if (report_status)
                progress::get().inform();
        }
    };

    scheduler::scheduler(size_t user_num_workers)
        : _impl { std::make_unique<impl>(user_num_workers) }
    {
    }

    scheduler::~scheduler() =default;

    size_t scheduler::num_workers() const
    {
        return _impl->num_workers();
    }

    size_t scheduler::cancel(const cancel_predicate_t &pred)
    {
        return _impl->cancel(pred);
    }

    void scheduler::submit(std::string name, int64_t priority, const task_func_t &task, std::optional<std::any> param)
    {
        _impl->post({ priority, std::move(name),task, std::move(param) });
    }

    void scheduler::on_error(const std::string &task_group, const error_observer_t &obs, const bool replace)
    {
        _impl->on_error(task_group, obs, replace);
    }

    bool scheduler::process_ok(bool report_status, const std::source_location &loc)
    {
        return _impl->process_ok(report_status, loc);
    }

    void scheduler::process(bool report_status, const std::source_location &loc)
    {
        _impl->process(report_status, loc);
    }

    void scheduler::process_once(const bool report_status)
    {
        _impl->process_once(report_status);
    }

    void scheduler::wait_all(const std::string &task_group, const wait_all_submit_func_t &submit_func)
    {
        return _impl->wait_all_done(task_group, submit_func);
    }
}