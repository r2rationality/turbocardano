/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cli/common.hpp>
#include <turbo/cardano/common/config.hpp>

namespace turbo::cli::utxo_genesis_view {
    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "utxo-genesis-view";
            cmd.desc = "print information about a genesis UTXO";
            cmd.args.names.emplace_back("<tx-hash>");
        }

        void run(const arguments &args) const override
        {
            const auto tx_hash = cardano::tx_hash::from_hex(args.at(0));
            for (const auto &[id, data]: cardano::config::get().byron_utxos) {
                if (id.hash == tx_hash)
                    logger::info("{}: {}", id, data);
            }
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}