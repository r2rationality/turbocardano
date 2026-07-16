/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/history.hpp>
#include "common.hpp"

namespace turbo::cli::tx_info {
    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "tx-info";
            cmd.desc = "show information about a transaction";
            cmd.args.expect({ "<data-dir>", "<tx-hash>" });
        }

        void run(const arguments &args) const override
        {
            const std::string &data_dir = args.at(0);
            const auto tx_hash = cardano::tx_hash::from_hex(args.at(1));
            chunk_registry cr { data_dir, chunk_registry::mode::index };
            reconstructor r { cr };
            const auto tx_info = r.find_tx(tx_hash);
            if (!tx_info)
                throw error(fmt::format("unknown transaction hash {}", tx_hash));
            std::cout << fmt::format("{}\n", *(*tx_info));
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}