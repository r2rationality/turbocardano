/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/test.hpp>
#include <turbo/config.hpp>
#include <turbo/plutus/uplc.hpp>

using namespace turbo;
using namespace turbo::plutus;
using namespace turbo::plutus::uplc;

suite plutus_uplc_suite = [] {
    "plutus::uplc"_test = [] {
        "conformance scripts"_test = [&] {
            const auto paths = file::files_with_ext_path(install_path("./data/plutus/conformance"), ".uplc");
            size_t ok = 0;
            for (const auto &path: paths) {
                try {
                    plutus::allocator alloc {};
                    script s { alloc, file::read(path.string()) };
                    ++ok;
                } catch (...) {
                    const auto exp_path = (path.parent_path() / (path.stem().string() + ".uplc.expected")).string();
                    if (std::filesystem::exists(exp_path)) {
                        const std::string exp_res { file::read(exp_path).str() };
                        if (exp_res == "parse error") {
                            ++ok;
                            continue;
                        }
                    }
                    logger::error("{}: parse failed", path);
                }
            }
            expect_equal(paths.size(), ok);
        };
    };
};