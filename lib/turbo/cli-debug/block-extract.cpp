/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cli/common.hpp>
#include <turbo/cardano.hpp>
#include <turbo/file.hpp>

namespace turbo::cli::debug::block_extract {
    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "block-extract";
            cmd.desc = "extract blocks with given slots from a chunk file";
            cmd.args.expect({ "<input-path>", "<output-path>", "<slot>", "[<slot> ...]" });
        }

        void run(const arguments &args) const override
        {
            const std::string &in_path { args.at(0) }, out_path { args.at(1) };
            std::set<uint64_t> slots {};
            for (const auto &slot: std::ranges::subrange(args.begin() + 2, args.end()))
                slots.emplace(std::stoull(slot));
            const auto buf = file::read(in_path);
            cbor::zero2::decoder dec { buf };
            file::write_stream ws { out_path };
            while (!dec.done()) {
                auto &block_tuple = dec.read();
                const cardano::block_container blk { numeric_cast<uint64_t>(block_tuple.data_begin() - buf.data()), block_tuple };
                if (slots.contains(blk->slot())) {
                    logger::info("found block at slot {} - extracting it", blk->slot());
                    ws.write(blk.raw());
                }
            }
        }
    };

    static auto instance = command::reg(std::make_shared<cmd>());
}