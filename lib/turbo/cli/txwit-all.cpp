/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/txwit/validator.hpp>
#include "common.hpp"

namespace turbo::cli::txwit_all {
    using namespace cardano;
    using namespace cardano::ledger;
    using namespace plutus;

    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "txwit-all";
            cmd.desc = "Validate all transaction witnesses and report the maximum valid slot";
            cmd.args.expect({ "<data-dir>" });
            cmd.opts.try_emplace("from-epoch", "only validate witnesses for blocks with the epoch number >= a given one");
            cmd.opts.try_emplace("to-epoch", "only validate witnesses for blocks with the epoch number <= a given one");
            cmd.opts.try_emplace("wits", "which witnesses to validate: vkey, native, plutus, all", "all");
        }

        void run(const arguments &args, const options &opts) const override {
            const chunk_registry cr { args.at(0), chunk_registry::mode::validate };
            optional_point from {};
            if (const auto opt_it = opts.find("from-epoch"); opt_it != opts.end() && opt_it->second) {
                const auto from_slot = slot::from_epoch(std::stoull(*opt_it->second), cr.config());
                const auto p = cr.latest_block_after_or_at_slot(from_slot);
                if (p == cr.cend())
                    throw error(fmt::format("can't find data for the first-epoch: {}", *opt_it->second));
                from.emplace(p->point());
            }
            optional_point to {};
            if (const auto opt_it = opts.find("to-epoch"); opt_it != opts.end() && opt_it->second) {
                const auto to_slot = static_cast<uint64_t>(slot::from_epoch(std::stoull(*opt_it->second) + 1, cr.config())) - 1;
                const auto p = cr.latest_block_before_or_at_slot(to_slot);
                if (p == cr.cend())
                    throw error(fmt::format("can't find data for the last-epoch: {}", *opt_it->second));
                to.emplace(p->point());
            }
            const txwit::witness_type typ = txwit::witness_type_from_str(opts.at("wits").value());
            std::atomic_size_t num_errs = 0;
            const auto valid_tip = txwit::validate(cr, from, to, typ, [&](const auto &) {
                num_errs.fetch_add(1, std::memory_order_relaxed);
            });
            logger::info("errors: {}", num_errs.load(std::memory_order_relaxed));
            logger::info("valid_tip: {}", valid_tip);
            logger::info("tip: {}", cr.tip());
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}
