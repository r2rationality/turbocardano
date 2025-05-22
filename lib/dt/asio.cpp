/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <chrono>
#include <map>
#ifdef __clang__
#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#ifdef _MSC_VER
#   include <SDKDDKVer.h>
#endif
#define BOOST_ASIO_HAS_STD_INVOKE_RESULT 1
#ifndef BOOST_ALLOW_DEPRECATED_HEADERS
#   define BOOST_ALLOW_DEPRECATED_HEADERS
#   define DT_CLEAR_BOOST_DEPRECATED_HEADERS
#endif
#include <boost/asio/ip/tcp.hpp>
#ifdef DT_CLEAR_BOOST_DEPRECATED_HEADERS
#   undef BOOST_ALLOW_DEPRECATED_HEADERS
#undef DT_CLEAR_BOOST_DEPRECATED_HEADERS
#endif
#ifdef __clang__
#   pragma GCC diagnostic pop
#endif
#include <dt/asio.hpp>
#include <dt/logger.hpp>
#include <dt/mutex.hpp>

namespace daedalus_turbo::asio {
    namespace net = boost::asio;
    using tcp = boost::asio::ip::tcp;

    struct worker_thread::impl {
        explicit impl() =default;

        ~impl()
        {
            _shutdown = true;
            _ioc.stop();
            _worker.join();
        }

        void add_before_action(const std::string &name, const action_type &act)
        {
            mutex::scoped_lock lk { _before_actions_mutex };
            auto [it, created] = _before_actions.try_emplace(name, act);
            if (!created)
                throw error(fmt::format("duplicate before action: {}", name));
        }

        void del_before_action(const std::string &name)
        {
            mutex::scoped_lock lk { _before_actions_mutex };
            if (_before_actions.erase(name) != 1)
                throw error(fmt::format("missing before action: {}", name));
        }

        void add_after_action(const std::string &name, const action_type &act)
        {
            mutex::scoped_lock lk { _after_actions_mutex };
            auto [it, created] = _after_actions.try_emplace(name, act);
            if (!created)
                throw error(fmt::format("duplicate after action: {}", name));
        }

        void del_after_action(const std::string &name)
        {
            mutex::scoped_lock lk { _after_actions_mutex };
            if (_after_actions.erase(name) != 1)
                throw error(fmt::format("missing after action: {}", name));
        }

        net::io_context &io_context()
        {
            return _ioc;
        }

        void internet_speed_report(const double current_speed)
        {
            if (current_speed > 0.0) {
                for (;;) {
                    double max_copy = _speed_max.load(std::memory_order_relaxed);
                    if (current_speed <= max_copy)
                        break;
                    if (_speed_max.compare_exchange_strong(max_copy, current_speed, std::memory_order_relaxed, std::memory_order_relaxed))
                        break;
                }
            }
            _speed_current.store(current_speed, std::memory_order_relaxed);
        }

        speed_mbps internet_speed() const
        {
            return { _speed_current.load(std::memory_order_relaxed), _speed_max.load(std::memory_order_relaxed) };
        }
    private:
        void _io_thread()
        {
            for (;;) {
                logger::run_log_errors([&] {
                    {
                        mutex::scoped_lock lk { _before_actions_mutex };
                        for (const auto &[name, act]: _before_actions)
                            logger::run_log_errors(act);
                    }
                    _ioc.run_for(std::chrono::milliseconds { 100 });
                    {
                        mutex::scoped_lock lk { _after_actions_mutex };
                        for (const auto &[name, act]: _after_actions)
                            logger::run_log_errors(act);
                    }
                });
                if (_shutdown)
                    break;
                if (_ioc.stopped())
                    _ioc.restart();
            }
        }

        std::atomic_bool _shutdown { false };
        net::io_context _ioc {};
        mutex::unique_lock::mutex_type _before_actions_mutex alignas(mutex::alignment) {};
        std::map<std::string, std::function<void()>> _before_actions {};
        mutex::unique_lock::mutex_type _after_actions_mutex alignas(mutex::alignment) {};
        std::map<std::string, std::function<void()>> _after_actions {};
        std::thread _worker { [&] { _io_thread(); } };
        std::atomic<double> _speed_max { 0.0 };
        std::atomic<double> _speed_current { 0.0 };
    };

    const worker_ptr &worker::get()
    {
        static worker_ptr w = std::make_shared<worker_thread>();
        return w;
    }

    worker_thread::worker_thread(): _impl { std::make_unique<impl>() }
    {
    }

    worker_thread::~worker_thread() =default;

    void worker_thread::add_before_action(const std::string &name, const action_type &act)
    {
        _impl->add_before_action(name, act);
    }

    void worker_thread::del_before_action(const std::string &name)
    {
        _impl->del_before_action(name);
    }

    void worker_thread::add_after_action(const std::string &name, const action_type &act)
    {
        _impl->add_after_action(name, act);
    }

    void worker_thread::del_after_action(const std::string &name)
    {
        _impl->del_after_action(name);
    }

    net::io_context &worker_thread::io_context()
    {
        return _impl->io_context();
    }

    void worker_thread::internet_speed_report(const double current_speed)
    {
        return _impl->internet_speed_report(current_speed);
    }

    speed_mbps worker_thread::internet_speed() const
    {
        return _impl->internet_speed();
    }

    struct worker_manual::impl {
        explicit impl() =default;

        ~impl()
        {
        }

        void add_before_action(const std::string &, const action_type &)
        {
            throw error("worker_manual::add_before_action not implemented");
        }

        void del_before_action(const std::string &)
        {
            throw error("worker_manual::del_before_action not implemented");
        }

        void add_after_action(const std::string &, const action_type &)
        {
            throw error("worker_manual::add_after_action not implemented");
        }

        void del_after_action(const std::string &)
        {
            throw error("worker_manual::del_after_action not implemented");
        }

        net::io_context &io_context()
        {
            return _ioc;
        }

        void internet_speed_report(const double)
        {
            throw error("worker_manual::internet_speed_report not implemented");
        }

        speed_mbps internet_speed() const
        {
            throw error("worker_manual::internet_speed not implemented");
        }
    private:
        net::io_context _ioc {};
    };

    worker_manual::worker_manual(): _impl { std::make_unique<impl>() }
    {
    }

    worker_manual::~worker_manual() =default;

    void worker_manual::add_before_action(const std::string &name, const action_type &act)
    {
        _impl->add_before_action(name, act);
    }

    void worker_manual::del_before_action(const std::string &name)
    {
        _impl->del_before_action(name);
    }

    void worker_manual::add_after_action(const std::string &name, const action_type &act)
    {
        _impl->add_after_action(name, act);
    }

    void worker_manual::del_after_action(const std::string &name)
    {
        _impl->del_after_action(name);
    }

    net::io_context &worker_manual::io_context()
    {
        return _impl->io_context();
    }

    void worker_manual::internet_speed_report(const double current_speed)
    {
        return _impl->internet_speed_report(current_speed);
    }

    speed_mbps worker_manual::internet_speed() const
    {
        return _impl->internet_speed();
    }
}
