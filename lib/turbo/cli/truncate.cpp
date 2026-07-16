/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/chunk-registry.hpp>
#include "common.hpp"

namespace turbo::cli::truncate {
    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "truncate";
            cmd.desc = "truncate the blockchain to the latest possible point before the end of epoch <max-epoch>";
            cmd.args.expect({ "<data-dir>", "<max-epoch>" });
        }

        void run(const arguments &args) const override
        {
            const auto &data_dir = args.at(0);
            const uint64_t epoch = std::stoull(args.at(1));
            chunk_registry cr { data_dir };
            cardano::optional_point max_block {};
            for (const auto &[last_byte_offset, chunk]: cr.chunks()) {
                if (cr.make_slot(chunk.last_slot).epoch() <= epoch) {
                    const cardano::point last_block { chunk.last_block_hash, chunk.last_slot,
                        chunk.blocks.back().height, chunk.blocks.back().end_offset() };
                    if (!max_block || *max_block < last_block)
                        max_block = last_block;
                }
            }
            cr.truncate(max_block);
            cr.remover().remove();
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}