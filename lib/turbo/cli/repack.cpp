/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cli/common.hpp>
#include <turbo/common/progress.hpp>

namespace turbo::cli::repack {
    struct cmd_t: command {
        void configure(config &cmd) const override
        {
            cmd.name = "repack";
            cmd.desc = "merge partial chunks and repack suboptimal chunks with the default zstd compression level";
            cmd.args.expect({ "<data-dir>" });
        }

        void run(const arguments &args) const override
        {
            progress_guard pg { "repack" };
            chunk_registry cr {
                args.at(0), chunk_registry_settings_t { .mode=chunk_registry::mode::store }
            };
            const auto stats = cr.repack();
            if (!stats.chunks_repacked)
                logger::info("repack: no chunks need repacking");
        }
    };
    static auto instance = command::reg(std::make_shared<cmd_t>());
}
