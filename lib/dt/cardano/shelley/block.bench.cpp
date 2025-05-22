/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/common/benchmark.hpp>
#include <dt/cardano.hpp>
#include <dt/file.hpp>
#include <dt/scheduler.hpp>

using namespace boost::ut;
using namespace daedalus_turbo;

suite cardano_shelley_bench_suite = [] {
    "cardano::shelley"_test = [&] {
        auto chunk = zstd::read("./data/chunk-registry/compressed/chunk/DF597E3FA352A7BD2F021733804C33729EBAA3DCAA9C0643BD263EFA09497B03.zstd");
        cbor::zero2::decoder dec { chunk };
        auto &block_tuple = dec.read();
        const cardano::block_container blk { numeric_cast<uint64_t>(block_tuple.data_begin() - chunk.data()), block_tuple };
        expect(blk->signature_ok());
        size_t num_iters = 10000;
        benchmark_r("shelley/signature_ok", 2000.0, 3, [&] {
            for (size_t i = 0; i < num_iters; ++i)
                blk->signature_ok();
            return num_iters;
        });
        scheduler sched {};
        benchmark_r("shelley/signature_ok parallel", 10000.0, 3, [&] {
            for (size_t i = 0; i < sched.num_workers(); ++i) {
                sched.submit("signature_ok", 100, [&]() {
                    for (size_t i = 0; i < num_iters; ++i)
                        blk->signature_ok();
                    return num_iters;
                });
            }
            sched.process(false);
            return sched.num_workers() * num_iters;
        });
    };
};