/* Copyright (c) 2026 R2 Rationality OÜ. License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */
#include <botan/base64.h>
#include <chrono>
#include <turbo/base58.hpp>
#include <turbo/base64.hpp>
#include <turbo/common/benchmark.hpp>
#include <turbo/config.hpp>

using namespace turbo;

suite base58_bench_suite = [] {
    "base58"_test = [] {
        const auto genesis = json::load(install_path("etc/preview/byron-genesis.json"));
        std::vector<std::pair<std::string, std::string>> inputs{};
        size_t decoded_bytes = 0;
        for (const auto &[address, balance]: genesis.at("nonAvvmBalances").as_object()) {
            const auto bytes = base58::decode(address);
            const auto encoded = Botan::base64_encode(bytes.data(), bytes.size());
            expect_equal(base64::decode(encoded), bytes);
            inputs.emplace_back(std::string{address}, encoded);
            decoded_bytes += bytes.size();
        }
        ankerl::nanobench::Bench bench{};
        bench.title("base58 vs base64")
            .output(&std::cerr)
            .unit("decoded byte")
            .batch(decoded_bytes)
            .warmup(1000)
            .minEpochIterations(10000)
            .minEpochTime(std::chrono::milliseconds{50})
            .relative(true);
        bench.run("base64 table", [&] {
            for (const auto &input: inputs)
                ankerl::nanobench::doNotOptimizeAway(base64::decode(input.second));
        });
        bench.run("base58 table", [&] {
            for (const auto &input: inputs)
                ankerl::nanobench::doNotOptimizeAway(base58::decode(input.first));
        });
    };
};
