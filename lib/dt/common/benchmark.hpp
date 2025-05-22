/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */
#ifndef DAEDALUS_TURBO_COMMON_BENCHMARK_HPP
#define DAEDALUS_TURBO_COMMON_BENCHMARK_HPP

#include <chrono>
#include <iostream>
#include <source_location>
#include <string>
#include <nanobench.h>
#include "test.hpp"

namespace daedalus_turbo {
    using namespace std;

    template<typename T>
    concept Countable = requires(T a) {
        { a() + 1 };
    };

    inline std::string humanize_rate(const double rate)
    {
        struct scale {
            double norm;
            const char *suffix;
        };
        static const vector<scale> scales { { 1e15, "P" }, { 1e12, "T" }, { 1e9, "G" }, { 1e6, "M" }, { 1e3, "K" } };
        const auto abs_rate = std::fabs(rate);
        for (const auto &[norm, suff]: scales) {
            if (abs_rate >= norm)
                return fmt::format("{:.3f} {}", rate / norm, suff);
        }
        return fmt::format("{:.3f} ", rate);
    }

    template<Countable T>
    static double benchmark_rate(const string_view name, const size_t num_iter, const T &action)
    {
        const auto start = std::chrono::high_resolution_clock::now();
        uint64_t total_iters = 0;
        for (size_t i = 0; i < num_iter; ++i) {
            total_iters += action();
        }
        const auto stop = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> sec = stop - start;
        const double rate = (double)total_iters / sec.count();
        std::clog << "[" << name << "] " << humanize_rate(rate) << "iters/sec"
            << ", total iters: " << total_iters << '\n';
        return rate;
    };

    template<typename T>
    static double benchmark_rate(const string_view name, const size_t num_iter, const T &action)
    {
        return benchmark_rate(name, num_iter, [&] {
            action();
            return 1;
        });
    };

    template<Countable T>
    static double benchmark_throughput(const string_view name, const size_t num_iter, const T &action)
    {
        const auto start = std::chrono::high_resolution_clock::now();
        uint64_t total_bytes = 0;
        for (size_t i = 0; i < num_iter; ++i) {
            total_bytes += action();
        }
        const std::chrono::duration<double> duration = std::chrono::high_resolution_clock::now() - start;
        const double sec = duration.count();
        const double rate = static_cast<double>(total_bytes) / sec;
        std::clog << fmt::format("[{}] {}B/sec, total bytes: {}\n", name, humanize_rate(rate), total_bytes);
        return rate;
    };

    template<Countable T>
    static void benchmark(const string_view name, const double min_rate, const size_t num_iter, const T &action)
    {
        boost::ut::test(name) = [=] {
            const double rate = benchmark_throughput(name, num_iter, action);
            boost::ut::expect(rate >= min_rate) << rate << " < " << min_rate;
        };
    }

    template<typename T>
    static void benchmark_r(const string_view name, const double min_rate, const size_t num_iter, const T &action)
    {
        boost::ut::test(name) = [=] {
            const double rate = benchmark_rate(name, num_iter, action);
            boost::ut::expect(rate >= min_rate) << rate << " < " << min_rate;
        };
    }
}

#endif // !DAEDALUS_TURBO_COMMON_BENCHMARK_HPP