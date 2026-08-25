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
                args.at(0), chunk_registry::mode::store, cardano::config::get(),
                scheduler::get(), file_remover::get(), false
            };
            const auto stats = cr.repack();
            logger::info(
                "repack complete: analyzed {} chunks, repacked {}, merged {} partial groups, compressed size {} -> {} bytes",
                stats.chunks_analyzed, stats.chunks_repacked, stats.partial_groups_merged,
                stats.compressed_size_before, stats.compressed_size_after);
        }
    };
    static auto instance = command::reg(std::make_shared<cmd_t>());
}
