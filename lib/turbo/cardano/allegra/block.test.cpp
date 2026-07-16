/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/test.hpp>
#include <turbo/plutus/context.hpp>

using namespace turbo;
using namespace turbo::cardano;

suite cardano_allegra_suite = [] {
    "cardano::allegra"_test = [] {
        "native scripts"_test = [] {
            const configs_dir cfg { configs_dir::default_path() };
            const cardano::config ccfg { cfg };
            ccfg.shelley_start_epoch(208);
            for (const auto &path: file::files_with_ext(install_path("data/allegra"), ".zpp")) {
                const plutus::context ctx { path, ccfg };
                const auto wit_cnts = ctx.tx().witnesses_ok();
                expect_equal(true, wit_cnts);
            }
        };
    };
};