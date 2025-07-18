/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include "const-iterator.hpp"
#include <turbo/chunk-registry.hpp>
#include <turbo/common/test.hpp>

namespace {
    using namespace turbo;
    using namespace turbo::cbor;
    using namespace turbo::cbor::zero2;
}

suite storage_const_iterator_bench_suite = [] {
    using namespace turbo::storage;
    using const_iterator = storage::const_iterator;
    "storage::const_iterator"_test = [] {
        static std::string data_dir = install_path("./data/chunk-registry");
        const chunk_registry cr { data_dir, chunk_registry::mode::store };

        ankerl::nanobench::Bench b {};
        b.title("storage::const_iterator")
            .output(&std::cerr)
            .unit("block")
            .performanceCounters(true)
            .relative(true);
        b.batch(cr.num_blocks());
        b.run("forward iterator",[&] {
            size_t num_blocks = 0;
            for (auto it = cr.cbegin(), end = cr.cend(); it != end; ++it ) {
                ++num_blocks;
            }
            ankerl::nanobench::doNotOptimizeAway(num_blocks);
        });
        b.run("reverse iterator",[&] {
            size_t num_blocks = 0;
            for (auto it = cr.crbegin(), end = cr.crend(); it != end; ++it ) {
                ++num_blocks;
            }
            ankerl::nanobench::doNotOptimizeAway(num_blocks);
        });
    };
};
