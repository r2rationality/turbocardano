/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/chunk-registry.hpp>
#include "common.hpp"

namespace turbo::cli::node_export {
    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "node-export";
            cmd.desc = "export blockchain data and state from <data-dir> in Cardano Node's format to <node-dir>";
            cmd.args.expect({ "<data-dir>", "<node-dir>" });
            cmd.opts.try_emplace("ledger-only", "export only the ledger state");
            cmd.opts.try_emplace("slot", "try to export the ledger state at a given slot", "latest");
        }

        void run(const arguments &args, const options &opts) const override
        {
            const auto &data_dir = args.at(0);
            const std::filesystem::path node_dir { args.at(1) };
            chunk_registry cr { data_dir };
            if (auto state_tip = cr.immutable_tip(); state_tip) {
                if (const auto opt_it = opts.find("slot"); opt_it != opts.end()) {
                    if (*opt_it->second == "latest") {
                        state_tip = cr.tip();
                    } else {
                        state_tip = cr.find_block_by_slot(std::stoull(*opt_it->second)).point();
                    }
                }
                cr.node_export(node_dir, *state_tip, opts.contains("ledger-only"));
                logger::info("exported state for the tip: {}", *state_tip);
            } else {
                logger::warn("the chain is too short and does not have an immutable tip yet - nothing to export!");
            }
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}