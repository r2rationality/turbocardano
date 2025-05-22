/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/cli.hpp>
#include <dt/cli/common.hpp>
#include <dt/chunk-registry.hpp>
#include <dt/memory.hpp>

namespace daedalus_turbo::cli::tip {
    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "tip";
            cmd.desc = "show the the last validated block and perform maintenance if necessary";
            cmd.args.expect({ "<data-dir>" });
            common::add_opts(cmd);
        }

        void run(const arguments &args, const options &opts) const override
        {
            const auto &data_dir = args.at(0);
            const auto mode = common::cr_mode(opts);
            const chunk_registry cr { data_dir, mode };
            const auto tip = cr.tip();
            logger::info("the local tip: {}", tip);
            if (mode == chunk_registry::mode::validate) {
                logger::info("the local core tip: {}", cr.core_tip());
                logger::info("the local immutable tip: {}", cr.immutable_tip());
                for (const auto &snap: cr.validator().snapshots()) {
                    logger::info("snapshot: {}", snap);
                }
            }
            if (tip)
                logger::info("the latest slot: {}", cr.make_slot(tip->slot));
            logger::info("peak memory usage: {} MB", memory::max_usage_mb());
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}