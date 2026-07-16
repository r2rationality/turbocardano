/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include "zero2.hpp"
#include <turbo/common/benchmark.hpp>
#include <turbo/config.hpp>

namespace {
    using namespace turbo;
    using namespace turbo::cbor;
    using namespace turbo::cbor::zero2;
}

suite cbor_zero2_bench_suite = [] {
    "cbor::zero2"_test = [] {
        ankerl::nanobench::Bench b {};
            b.title("cbor::zero2")
            .output(&std::cerr)
            .unit("byte")
            .performanceCounters(true)
            .relative(true);
        {
            const auto data = file::read(install_path("data/conway/block-0.cbor"));
            b.batch(data.size());
            b.run("decoder block",[&] {
                decoder dec { data };
                while (!dec.done()) {
                    ankerl::nanobench::doNotOptimizeAway(dec.read());
                }
            });
        }
        {
            const auto data = zstd::read(install_path("data/chunk-registry/chang/9326B83719AEAB06A671EA653EE297F1DA601A4FC279A759503D79F55DA6EEC7.zstd"));
            b.batch(data.size());
            b.run("decoder chunk",[&] {
                decoder dec { data };
                while (!dec.done()) {
                    ankerl::nanobench::doNotOptimizeAway(dec.read());
                }
            });
        }
    };
};
