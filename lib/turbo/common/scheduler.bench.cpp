/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include <filesystem>
#include <random>
#include <turbo/common/benchmark.hpp>
#include <turbo/common/coro.hpp>
#include <turbo/common/zstd.hpp>
#include "numeric-cast.hpp"
#include "scheduler.hpp"
#ifdef _MSC_VER
#   include <SDKDDKVer.h>
#endif
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/post.hpp>

namespace {
    using namespace turbo;

    template<std::random_access_iterator IT>
    void sum_batch(std::atomic<double> &total_time, const IT begin, const IT end) {
        const auto start_time = std::chrono::system_clock::now();
        double sum = 0.0;
        for (auto it = begin; it != end; ++it) {;
            const auto &val = *it;
            sum += std::sqrt(val * val);
        }
        total_time.fetch_add(
            std::chrono::duration<double> { std::chrono::system_clock::now() - start_time }.count() * 1000,
            std::memory_order_relaxed
        );
    }

    template<std::random_access_iterator IT>
    boost::asio::awaitable<void> sum_batch_boost_coro(std::atomic<double> &total_time, const IT begin, const IT end)
    {
        sum_batch(total_time, begin, end);
        co_return;
    }

    template<std::random_access_iterator IT>
    coro::task_t<void> sum_batch_coro(std::atomic<double> &total_time, const IT begin, const IT end)
    {
        sum_batch(total_time, begin, end);
        co_return;
    }

    uint8_vector make_test_data_1(const size_t size)
    {
        uint8_vector res {};
        res.reserve(size);
        for (size_t i = 0; res.size() < size; ++i)
            res << buffer::from(i);
        return res;
    }

    uint8_vector make_test_data_2(const size_t size)
    {
        uint8_vector res {};
        res.reserve(size);
        std::random_device rd{};
        std::mt19937 gen{rd()};
        std::uniform_int_distribution<uint16_t> dist{0, 255};
        while (res.size() < size)
            res.emplace_back(numeric_cast<uint8_t>(dist(gen)));
        return res;
    }
}

suite turbo_common_scheduler_bench_suite = [] {
    "turbo::common::scheduler"_test = [] {
        auto &sched = scheduler::get();
        std::vector<double> tasks {};
        for (size_t i = 0; i < 1'000'000; ++i)
            tasks.emplace_back(static_cast<double>(i));
        ankerl::nanobench::Bench b {};
        b.title("turbo::scheduler")
            .output(&std::cerr)
            .performanceCounters(true)
            .relative(true);
        {
            size_t data_multiple = 20;
            std::vector<uint8_vector> chunks;
            size_t total_size = 0;
            total_size += chunks.emplace_back(make_test_data_1(8ULL << 20U)).size();
            total_size += chunks.emplace_back(make_test_data_2(8ULL << 20U)).size();
            b.batch(total_size);
            b.unit("byte");
            b.run("scheduler/default progress update", [&] {
                for (size_t i = 0; i < data_multiple; ++i) {
                    for (const auto &chunk: chunks) {
                        sched.submit(
                            "compress", 0,
                            [&chunk]() {
                                uint8_vector tmp;
                                zstd::compress(tmp, chunk, 3);
                            }
                        );
                    }
                }
                sched.process();
            });
        }
        b.unit("task");
        b.batch(tasks.size());
        for (size_t batch_size: { 10, 100, 1'000, 10'000 }) {
            b.run(fmt::format("nano tasks: scheduler - batch {}", batch_size), [&]() {
                std::atomic<double> total_time { 0.0 };
                for (size_t start = 0; start < tasks.size(); start += batch_size) {
                    auto end = std::min(start + batch_size, tasks.size());
                    sched.submit("math", 0, [&tasks, &total_time, start, end]() {
                        const auto start_time = std::chrono::system_clock::now();
                        double sum = 0.0;
                        for (size_t i = start; i < end; ++i) {
                            const auto &val = tasks[i];
                            sum += std::sqrt(val * val);
                        }
                        total_time.fetch_add(
                            std::chrono::duration<double> { std::chrono::system_clock::now() - start_time }.count() * 1000,
                            std::memory_order_relaxed
                        );
                        ankerl::nanobench::doNotOptimizeAway(sum);
                    });
                }
                sched.process();
            });
        }
        for (size_t batch_size: { 10, 100, 1'000, 10'000 }) {
            b.run(fmt::format("nano tasks: scheduler coro - batch {}", batch_size), [&]() {
                std::atomic<double> total_time { 0.0 };
                for (size_t start = 0; start < tasks.size(); start += batch_size) {
                    auto end = std::min(start + batch_size, tasks.size());
                    auto coro = std::make_shared<coro::task_t<void>>(sum_batch_coro(total_time, tasks.begin() + start, tasks.begin() + end));
                    sched.submit("math", 0, [coro=std::move(coro)]() mutable {
                        coro->resume();
                    });
                }
                sched.process();
            });
        }
        {
            boost::asio::thread_pool tp {};
            for (size_t batch_size: { 10, 100, 1000, 10'000 }) {
                b.run(fmt::format("nano task: io_context: batch: {}", batch_size), [&]() {
                    boost::asio::thread_pool tp {};
                    std::atomic<double> total_time { 0.0 };
                    for (size_t start = 0; start < tasks.size(); start += batch_size) {
                        const auto end = std::min(start + batch_size, tasks.size());
                        boost::asio::post(tp, [&tasks, &total_time, start, end] {
                            const auto start_time = std::chrono::system_clock::now();
                            double sum = 0.0;
                            for (size_t i = start; i < end; ++i) {
                                const auto &val = tasks[i];
                                sum += std::sqrt(val * val);
                            }
                            total_time.fetch_add(
                                std::chrono::duration<double> { std::chrono::system_clock::now() - start_time }.count() * 1000,
                                std::memory_order_relaxed
                            );
                            ankerl::nanobench::doNotOptimizeAway(sum);
                        });
                    }
                    tp.join();
                });
            }
        }
        {
            boost::asio::thread_pool tp {};
            for (size_t batch_size: { 10, 100, 1'000, 10'000 }) {
                b.run(fmt::format("nano task: io_context coro: batch: {}", batch_size), [&]() {
                    boost::asio::thread_pool tp {};
                    std::atomic<double> total_time { 0.0 };
                    for (size_t start = 0; start < tasks.size(); start += batch_size) {
                        const auto end = std::min(start + batch_size, tasks.size());
                        boost::asio::co_spawn(
                            tp,
                            sum_batch_boost_coro(total_time, tasks.begin() + start, tasks.begin() + end),
                            boost::asio::detached
                        );
                    }
                    tp.join();
                });
            }
        }
    };
};