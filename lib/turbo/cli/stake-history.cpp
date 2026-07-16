/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <algorithm>
#include <turbo/cardano.hpp>
#include <turbo/history.hpp>
#include "common.hpp"

namespace turbo::cli::stake_history {
    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "stake-history";
            cmd.desc = "list all transactions referencing a given stake address";
            cmd.args.expect({ "<data-dir>", "<pay-addr>" });
        }

        void run(const arguments &args) const override
        {
            timer t { "reconstruction and serialization", logger::level::debug };
            const auto &data_dir = args.at(0);
            cardano::address_buf addr_raw { args.at(1) };
            if (addr_raw.size() == 28)
                addr_raw.insert(addr_raw.begin(), 0xE1);
            chunk_registry cr { data_dir, chunk_registry::mode::index };
            reconstructor r { cr };
            cardano::address addr { addr_raw };
            const auto id = addr.stake_id();
            std::cout << fmt::format("{}", r.find_history(id));
        }
    };

    static auto instance = command::reg(std::make_shared<cmd>());
}