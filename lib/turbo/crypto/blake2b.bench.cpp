/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/common/benchmark.hpp>
#include <turbo/common/scheduler.hpp>
#include <turbo/common/zstd.hpp>
#include <turbo/crypto/blake2b.hpp>

namespace {
    using namespace turbo;
    using namespace turbo::crypto;
}

suite crypto_blake2b_bench_suite = [] {
    "crypto::blake2b"_test = [&] {
        auto in = zstd::read("./data/chunk-registry/compressed/chunk/47F62675C9B0161211B9261B7BB1CF801EDD4B9C0728D9A6C7A910A1581EED41.zstd");
        scheduler sched {};
        static const std::string name = "blake2b";
        size_t num_evals = (1 << 30) / in.size();
        for (size_t hash_size_bits : { 224, 256 }) {
            size_t out_len = hash_size_bits / 8;
            uint8_vector out {};
            out.resize(out_len);
            benchmark(
                std::string { name } + "/" + std::to_string(hash_size_bits),
                [&] {
                    for (size_t i = 0; i < num_evals; ++i)
                        blake2b::digest(out, in);;
                },
                in.size() * num_evals
            );
        }
        size_t num_evals_par = num_evals * 32;
        benchmark(
            name + std::string { "-parallel" },
            [&] {
                for (size_t i = 0; i < num_evals_par; ++i)
                    sched.submit("hash", 100, [&]() {
                        blake2b::hash_32 out;
                        blake2b::digest(out, in);
                    });
                sched.process(false);
            },
            in.size() * num_evals_par
        );
    };
};