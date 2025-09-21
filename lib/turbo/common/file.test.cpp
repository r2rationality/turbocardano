/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include <turbo/common/test.hpp>
#include <turbo/common/scope-exit.hpp>
#include "logger.hpp"

namespace {
    using namespace turbo;
}

suite turbo_common_file_suite = [] {
    using boost::ext::ut::v2_1_0::nothrow;
    "turbo::common::file"_test = [] {
        "install_path"_test = [] {
            const auto orig_install_dir = file::install_path("");
            expect_equal(std::filesystem::current_path().string(), orig_install_dir);
            const scope_exit cleanup{[&]{ file::set_install_path_exact(orig_install_dir + ""); }};
            expect(throws([]{ file::set_install_path("/cli"); }));
            file::set_install_path("/bin/cli");
            expect_equal("/log", file::install_path("log"));
            file::set_install_path("/opt/turbo/bin/cli");
            expect_equal("/opt/turbo/log", file::install_path("log"));
            file::set_install_path("D:\\bin\\cli");
            expect_equal("D:\\log", file::install_path("log"));
        };
    };
};