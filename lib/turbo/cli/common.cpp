/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cli/common.hpp>

namespace turbo::cli::common {
    void add_opts(config &cmd)
    {
        cmd.opts.try_emplace("mode", "<store|index|validate> the activities to performed during synchronization", "validate");
    }

    chunk_registry::mode cr_mode(const options &opts)
    {
        const auto &mode_s = opts.at("mode").value();
        if (mode_s == "store")
            return chunk_registry::mode::store;
        if (mode_s == "index")
            return chunk_registry::mode::index;
        if (mode_s == "validate")
            return chunk_registry::mode::validate;
        throw error(fmt::format("unsupported mode: '{}'", mode_s));
    }
}
