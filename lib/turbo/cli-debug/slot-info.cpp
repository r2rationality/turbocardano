/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cli/common.hpp>
#include <turbo/cardano.hpp>

namespace turbo::cli::debug::slot_info {
    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "slot-info";
            cmd.desc = "print information about the epoch and epoch's slot";
            cmd.args.expect({ "<slot-number>" });
            cmd.opts.emplace("shelley-start-epoch", "the first epoch of the Shelley hard fork");
        }

        void run(const arguments &args, const options &opts) const override
        {
            const auto &cfg = cardano::config::get();
            if (const auto opt_it = opts.find("shelley-start-epoch"); opt_it != opts.end() && opt_it->second)
                cfg.shelley_start_epoch(std::stoull(*opt_it->second));
            cardano::slot slot { std::stoull(args.at(0)), cfg };
            logger::info("slot: {} timestamp: {}", slot, slot.timestamp());
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}