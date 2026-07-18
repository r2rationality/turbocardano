/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/test.hpp>
#include <turbo/plutus/conformance-data.hpp>
#include <turbo/plutus/uplc.hpp>

using namespace turbo;
using namespace turbo::plutus;
using namespace turbo::plutus::uplc;

suite plutus_uplc_suite = [] {
    "plutus::uplc"_test = [] {
        "generated names"_test = [] {
            plutus::allocator alloc {};
            const script s { alloc, uint8_vector { std::string_view { "(program 1.0.0 (lam j_1-0 j_1-0))" } } };
            expect_equal("(lam v0 v0)", fmt::format("{}", s.program()));
        };
        "generated names preserve nested bindings"_test = [] {
            plutus::allocator alloc {};
            const script s { alloc, uint8_vector { std::string_view { "(program 1.0.0 (lam outer (lam inner outer)))" } } };
            expect_equal("(lam v0 (lam v1 v0))", fmt::format("{}", s.program()));
        };
        "open term"_test = [] {
            plutus::allocator alloc {};
            const script s { alloc, uint8_vector { std::string_view { "(program 1.0.0 x)" } } };
            expect_equal("v0", fmt::format("{}", s.program()));
        };
        "conformance scripts"_test = [&] {
            const auto paths = file::files_with_ext_path(conformance_data_dir().string(), ".uplc");
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
