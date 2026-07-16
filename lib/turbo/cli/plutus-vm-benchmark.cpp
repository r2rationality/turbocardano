/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <cmath>
#include <iostream>
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

    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "plutus-vm-benchmark";
            cmd.desc = "benchmark raw Flat decode and Plutus VM evaluation, writing benchmark-suite CSV output";
            cmd.args.expect({ "<script-dir>", "<output-csv>" });
            cmd.opts.try_emplace("iterations", "measured evaluations per script", "50");
            cmd.opts.try_emplace("warmup", "unmeasured warmup iterations per script", "5");
        }

        void run(const arguments &args, const options &opts) const override
        {
            const auto iterations = parse_iterations(opts);
            const auto warmup = parse_warmup(opts);
            auto paths = file::files_with_ext_path(args.at(0), ".flat");
            std::ranges::sort(paths);
            if (paths.empty())
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

                    ankerl::nanobench::Bench bench {};
                    bench.title("TurboCardano raw Flat decode + Plutus VM evaluate")
                        .unit("script")
                        .epochs(iterations)
                        .epochIterations(1)
                        .performanceCounters(false)
                        .output(&std::cerr)
                        .run(name, [&] { evaluate(bytes); });

                    const auto &result = bench.results().back();
                    const auto mean = result.average(ankerl::nanobench::Result::Measure::elapsed);
                    const auto median = result.median(ankerl::nanobench::Result::Measure::elapsed);
                    const auto minimum = result.minimum(ankerl::nanobench::Result::Measure::elapsed);
                    const auto maximum = result.maximum(ankerl::nanobench::Result::Measure::elapsed);
                    const auto stddev = standard_deviation(result, mean);
                    out = fmt::format_to(out, "{},{},{},{},{},{},{},{}\n",
                        vm_name, name, to_ns(mean), to_ns(median), to_ns(minimum), to_ns(maximum),
                        to_ns(stddev), result.size());
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
        static size_t parse_iterations(const options &opts)
        {
            const auto iterations = from_str<size_t>(opts.at("iterations").value());
            if (iterations == 0)
                throw error("--iterations must be greater than zero");
            return iterations;
        }

        static size_t parse_warmup(const options &opts)
        {
            return from_str<size_t>(opts.at("warmup").value());
        }

        static void evaluate(const uint8_vector &bytes)
        {
            allocator alloc {};
            flat::script script { alloc, bytes, plutus_language, protocol_major, false };
            machine m { alloc, plutus_language, {}, protocol_major };
            const auto result = m.evaluate(script.program());
            ankerl::nanobench::doNotOptimizeAway(result.expr);
        }

        static double standard_deviation(const ankerl::nanobench::Result &result, const double mean)
        {
            if (result.empty())
                return 0.0;
            double squared_difference_sum = 0.0;
            for (size_t i = 0; i < result.size(); ++i) {
                const auto value = result.get(i, ankerl::nanobench::Result::Measure::elapsed);
                const auto difference = value - mean;
                squared_difference_sum += difference * difference;
            }
            return std::sqrt(squared_difference_sum / static_cast<double>(result.size()));
        }

        static uint64_t to_ns(const double seconds)
        {
            return numeric_cast<uint64_t>(std::llround(seconds * 1'000'000'000.0));
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}
