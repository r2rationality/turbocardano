#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/cli.hpp>
#include <turbo/chunk-registry.hpp>

namespace turbo::cli::common {
    extern void add_opts(config &cmd);
    extern chunk_registry::mode cr_mode(const options &opts);
}