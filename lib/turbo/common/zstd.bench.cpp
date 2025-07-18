/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include <turbo/common/benchmark.hpp>
#include "zstd.hpp"

using namespace turbo;

namespace {
    struct perf_exp {
        size_t zstd_level;
        double throughput;
    };
}

suite zstd_bench_suite = [] {
    "zstd"_test = [] {
        auto data = zstd::read("./data/chunk-registry/compressed/chunk/977E9BB3D15A5CFF5C5E48617288C5A731DB654C0B42D63627C690CEADC9E1F3.zstd");
        if (data.size() > (1 << 22))
            data.resize(1 << 24);
        for (const auto &[zstd_level, exp_throughput]: {
            perf_exp { 1, 300e6 },
            perf_exp { 3, 200e6 },
            perf_exp { 9, 50e6 },
            perf_exp { 22, 2e6 }
        }) {
            uint8_vector compressed {};
            benchmark("zstd::compress level " + std::to_string(zstd_level), [&] {
                zstd::compress(compressed, data, zstd_level);
            }, data.size());
            benchmark("zstd::decompress level " + std::to_string(zstd_level), [&] {
                uint8_vector out_data {};
                zstd::decompress(out_data, compressed);
            }, data.size());
        }
        benchmark("zstd::read", [] {
            ankerl::nanobench::doNotOptimizeAway(zstd::read("./data/chunk-registry/compressed/chunk/977E9BB3D15A5CFF5C5E48617288C5A731DB654C0B42D63627C690CEADC9E1F3.zstd"));
        }, data.size());
        file::tmp tmp_f { "zstd-write.tmp" };
        benchmark("zstd::write", [&] {
            zstd::write(tmp_f.path(), data);
        }, data.size());
    };
};