/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/common/test.hpp>
#include "base.hpp"

namespace {
    using namespace turbo;
    using namespace turbo::sync;
}

suite turbo_sync_base_suite = [] {
    "turbo::sync::base"_test = [] {
        optional_progress_point target{};
        expect_equal(false, optional_point{} < target);
        expect_equal(false, optional_point{point{{}, 0U}} < target);
    };
};