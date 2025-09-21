/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cli/common.hpp>
#include <turbo/cardano.hpp>
#include <turbo/file.hpp>

namespace turbo::cli::debug::block_txs {
    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "block-txs";
            cmd.desc = "list all transaction ids in a block or chunk of blocks";
            cmd.args.expect({ "<chunk-path>" });
        }

        void run(const arguments &args) const override
        {
            const std::string &in_path { args.at(0) };
            const auto buf = file::read(in_path);
            cbor::zero2::decoder dec { buf };
            for (size_t idx = 0; !dec.done(); ++idx) {
                auto &block_tuple = dec.read();
                const cardano::block_container blk { numeric_cast<uint64_t>(block_tuple.data_begin() - buf.data()), block_tuple };
                size_t tx_idx = 0;
                for (const auto &tx: blk->txs()) {
                    fmt::print("block #{} slot: {} tx #{}: {} offset: {} invalid: {}\n", idx, blk->slot_object(), tx_idx++, tx->hash(), tx->offset(), tx->invalid());
                }
            }
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}
