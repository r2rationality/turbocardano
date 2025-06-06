/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/common/benchmark.hpp>
#include <dt/config.hpp>
#include <dt/plutus/flat.hpp>

using namespace daedalus_turbo;

suite plutus_flat_suite = [] {
    "plutus::flat"_test = [] {
        const auto paths = file::files_with_ext(install_path("./data/plutus/script-v2"), ".bin");
        daedalus_turbo::vector<uint8_vector> data {};
        data.reserve(paths.size());
        for (const auto &path: paths) {
            data.emplace_back(file::read(path.string()));
        }
        benchmark("flat parse speed", 1e6, 4096, [&] {
            uint64_t total_size = 0;
            for (const auto &bytes: data) {
                total_size += bytes.size();
                plutus::allocator alloc {};
                plutus::flat::script s { alloc, bytes };
            }
            return total_size;
        });
    };
};