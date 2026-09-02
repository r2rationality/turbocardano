/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <nanobench.h>
#include <turbo/common/file.hpp>
#include <turbo/plutus/flat.hpp>
#include <turbo/plutus/machine.hpp>
#include "common.hpp"

namespace turbo::cli::plutus_vm_benchmark {
    using namespace cardano;
    using namespace plutus;

    static constexpr auto vm_name = "turbocardano";
    static constexpr auto plutus_language = script_type::plutus_v3;
    static constexpr uint64_t protocol_major = 11;

    struct measurement_stats {
        uint64_t mean_ns = 0;
        uint64_t median_ns = 0;
        uint64_t min_ns = 0;
        uint64_t max_ns = 0;
        uint64_t stddev_ns = 0;
        size_t iterations = 0;
        std::chrono::nanoseconds total_time {};
    };

    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "plutus-vm-benchmark";
            cmd.desc = "benchmark raw Flat decode and Plutus VM evaluation, writing benchmark-suite CSV output";
            cmd.args.expect({ "<script-dir>", "<output-csv>" });
            cmd.opts.try_emplace("iterations", "minimum measured evaluations per script", "50");
            cmd.opts.try_emplace("min-time", "minimum measured time per script", "1s");
            cmd.opts.try_emplace("max-iterations", "maximum measured evaluations per script", "10000");
            cmd.opts.try_emplace("warmup", "unmeasured warmup iterations per script", "5");
        }

        void run(const arguments &args, const options &opts) const override
        {
            const auto min_iterations = parse_positive_size(opts, "iterations");
            const auto min_time = parse_duration(opts.at("min-time").value());
            const auto max_iterations = parse_positive_size(opts, "max-iterations");
            const auto warmup = from_str<size_t>(opts.at("warmup").value());
            if (max_iterations < min_iterations) [[unlikely]]
                throw error("--max-iterations must be greater than or equal to --iterations");
            auto paths = file::files_with_ext_path(args.at(0), ".flat");
            std::ranges::sort(paths);
            if (paths.empty()) [[unlikely]]
                throw error(fmt::format("no .flat scripts found in {}", args.at(0)));

            std::string csv { "vm,script,mean_ns,median_ns,min_ns,max_ns,stddev_ns,iterations\n" };
            auto out = std::back_inserter(csv);
            size_t succeeded = 0;
            for (const auto &path: paths) {
                const auto name = path.stem().string();
                try {
                    const auto bytes = file::read(path.string());
                    for (size_t i = 0; i < warmup; ++i)
                        evaluate(bytes);

                    const auto stats = measure(bytes, min_iterations, min_time, max_iterations);
                    if (stats.iterations == max_iterations && stats.total_time < min_time) {
                        logger::warn("benchmark {} reached --max-iterations={} after {:.3f}s, before --min-time={:.3f}s",
                            name, max_iterations, to_seconds(stats.total_time), to_seconds(min_time));
                    }
                    std::cerr << fmt::format("{}: mean {} ns, median {} ns, CV {}, {} iterations, {:.3f}s measured\n",
                        name, stats.mean_ns, stats.median_ns,
                        stats.mean_ns > 0 ? static_cast<double>(stats.stddev_ns) / stats.mean_ns : 0.0,
                        stats.iterations, to_seconds(stats.total_time));
                    out = fmt::format_to(out, "{},{},{},{},{},{},{},{}\n",
                        vm_name, name, stats.mean_ns, stats.median_ns, stats.min_ns, stats.max_ns,
                        stats.stddev_ns, stats.iterations);
                    ++succeeded;
                } catch (const std::exception &ex) {
                    logger::error("EVAL_FAIL: {}: {}", name, ex.what());
                    out = fmt::format_to(out, "{},{},-1,-1,-1,-1,-1,0\n", vm_name, name);
                }
            }
            file::write(args.at(1), csv);
            logger::info("benchmarked {} scripts ({} succeeded, {} failed); results: {}",
                paths.size(), succeeded, paths.size() - succeeded, args.at(1));
        }

    private:
        static size_t parse_positive_size(const options &opts, const std::string_view name)
        {
            const auto value = from_str<size_t>(opts.at(std::string { name }).value());
            if (value == 0) [[unlikely]]
                throw error(fmt::format("--{} must be greater than zero", name));
            return value;
        }

        static std::chrono::nanoseconds parse_duration(const std::string &text)
        {
            static constexpr std::array units {
                std::pair { std::string_view { "ns" }, 1.0 },
                std::pair { std::string_view { "us" }, 1'000.0 },
                std::pair { std::string_view { "ms" }, 1'000'000.0 },
                std::pair { std::string_view { "s" }, 1'000'000'000.0 },
            };
            const std::string_view view { text };
            for (const auto &[suffix, multiplier]: units) {
                if (!view.ends_with(suffix))
                    continue;
                const auto number = view.substr(0, view.size() - suffix.size());
                size_t parsed = 0;
                double value = 0.0;
                try {
                    value = std::stod(std::string { number }, &parsed);
                } catch (const std::exception &) {
                    throw error(fmt::format("invalid --min-time value: {}", text));
                }
                if (parsed != number.size() || !std::isfinite(value) || value <= 0.0) [[unlikely]]
                    throw error(fmt::format("invalid --min-time value: {}", text));
                const auto nanos = value * multiplier;
                if (!std::isfinite(nanos) || nanos > static_cast<double>(std::numeric_limits<int64_t>::max())) [[unlikely]]
                    throw error(fmt::format("--min-time value is too large: {}", text));
                if (nanos < 1.0) [[unlikely]]
                    throw error(fmt::format("--min-time must be at least 1ns: {}", text));
                return std::chrono::nanoseconds { numeric_cast<int64_t>(std::llround(nanos)) };
            }
            throw error(fmt::format("invalid --min-time value '{}': expected ns, us, ms, or s", text));
        }

        static void evaluate(const uint8_vector &bytes)
        {
            allocator alloc {};
            flat::script script { alloc, bytes, plutus_language, protocol_major, false };
            machine m { alloc, plutus_language, {}, protocol_major };
            const auto result = m.evaluate(script.program());
            ankerl::nanobench::doNotOptimizeAway(result.expr);
        }

        static measurement_stats measure(const uint8_vector &bytes, const size_t min_iterations,
            const std::chrono::nanoseconds min_time, const size_t max_iterations)
        {
            using clock = std::chrono::steady_clock;
            std::vector<uint64_t> samples {};
            samples.reserve(max_iterations);
            std::chrono::nanoseconds total_time {};
            while ((samples.size() < min_iterations || total_time < min_time)
                    && samples.size() < max_iterations) {
                const auto start = clock::now();
                evaluate(bytes);
                const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - start);
                samples.emplace_back(numeric_cast<uint64_t>(elapsed.count()));
                total_time += elapsed;
            }

            long double sum = 0.0;
            for (const auto sample: samples)
                sum += static_cast<long double>(sample);
            const auto mean = sum / static_cast<long double>(samples.size());

            long double squared_difference_sum = 0.0;
            for (const auto sample: samples) {
                const auto difference = static_cast<long double>(sample) - mean;
                squared_difference_sum += difference * difference;
            }

            std::ranges::sort(samples);
            const auto middle = samples.size() / 2;
            const auto median = samples.size() % 2 == 0
                ? samples[middle - 1] + (samples[middle] - samples[middle - 1]) / 2
                : samples[middle];
            return {
                numeric_cast<uint64_t>(std::llround(mean)),
                median,
                samples.front(),
                samples.back(),
                numeric_cast<uint64_t>(std::llround(std::sqrt(
                    squared_difference_sum / static_cast<long double>(samples.size())))),
                samples.size(),
                total_time,
            };
        }

        static double to_seconds(const std::chrono::nanoseconds duration)
        {
            return std::chrono::duration<double> { duration }.count();
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}
