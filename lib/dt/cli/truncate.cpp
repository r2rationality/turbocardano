/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2024 Alex Sierkov (alex dot sierkov at gmail dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */
#include <dt/cli.hpp>
#include <dt/requirements.hpp>
#include <dt/chunk-registry.hpp>

namespace daedalus_turbo::cli::truncate {
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
            requirements::check(data_dir);
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