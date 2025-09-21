/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cli/common.hpp>
#include <turbo/history.hpp>

namespace turbo::cli::debug::tx_view {
    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "tx-view";
            cmd.desc = "print CBOR data of a given transaction";
            cmd.args.expect({ "<data-dir>", "<tx-hash>" });
            cmd.opts.try_emplace("raw-wits", "print only the raw data of witnesses");
        }

        void run(const arguments &args) const override
        {
            const auto &data_dir = args.at(0);
            const auto tx_hash = uint8_vector::from_hex(args.at(1));
            //const bool raw_wits = opts.contains("raw-wits");
            chunk_registry cr { data_dir, chunk_registry::mode::index };
            reconstructor r { cr };
            const auto tx_info = r.find_tx(tx_hash);
            if (!tx_info) [[unlikely]]
                throw error(fmt::format("unknown transaction hash {}", tx_hash));
            auto block_tuple = cr.read((*tx_info)->block().offset());
            const cardano::block_container blk { (*tx_info)->block().offset(), block_tuple.get(), cr.config() };
            blk->foreach_tx([&](const auto &tx) {
                if (tx.hash() == tx_hash) {
                    logger::info("tx-view offset {} invalid: {} size: {} hash: {} slot: {}",
                        (*tx_info)->block().offset(), (*tx_info)->invalid(), (*tx_info)->raw().size(), tx.hash(), blk->slot_object());
                    logger::info("tx_data: {}", cbor::zero2::parse(tx.raw()).get().to_string());
                    logger::info("wit_data: {}", cbor::zero2::parse(tx.witness_raw()).get().to_string());
                }
            });
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}