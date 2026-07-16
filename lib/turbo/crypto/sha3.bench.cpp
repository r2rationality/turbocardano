/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <chrono>
#include <turbo/common/benchmark.hpp>
#include <turbo/common/zstd.hpp>
#include <turbo/crypto/sha3.hpp>

using namespace turbo;
using namespace turbo::crypto;

suite crypto_sha3_bench_suite = [] {
    "crypto::sha3"_test = [&] {
        const auto in = zstd::read("./data/chunk-registry/compressed/chunk/47F62675C9B0161211B9261B7BB1CF801EDD4B9C0728D9A6C7A910A1581EED41.zstd");
        const size_t num_evals = (1 << 30) / in.size();
        benchmark(
            "sha3",
            [&] {
                sha3::hash_256 out {};
                for (size_t i = 0; i < num_evals; ++i)
                    sha3::digest(out, in);
            },
            in.size() * num_evals
        );
    };
};