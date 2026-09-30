/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/chunk-registry.hpp>
#include <turbo/common/scope-exit.hpp>
#include <turbo/sync/base.hpp>
#include "common.hpp"

namespace turbo::cli::validate {
    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "revalidate";
            cmd.desc = "revalidate the blockchain in <data-dir> from scratch";
            cmd.args.expect({ "<data-dir>" });
            cmd.opts.emplace("max-epoch", "validate and keep data only up to and including this epoch");
            cmd.opts.try_emplace("validation", "validation mode to use: none, turbo, full", "turbo");
        }

        void run(const arguments &args, const options &opts) const override
        {
            timer t { "validation", logger::level::trace };
            const auto &data_dir = args.at(0);
            std::optional<uint64_t> max_epoch {};
            if (const auto opt_it = opts.find("max-epoch"); opt_it != opts.end() && opt_it->second)
                max_epoch = std::stoull(*opt_it->second);
            const auto validation_mode = sync::validation_mode_from_text(opts.at("validation").value());
            progress_guard pg { "parse", "merge", "validate" };
            chunk_registry cr { data_dir };
            auto max_block = cr.tip();
            if (max_epoch) {
                max_block.reset();
                for (const auto &[offset, chunk]: cr.chunks()) {
                    if (cr.make_slot(chunk.last_block.slot).epoch() > *max_epoch) {
                        break;
                    }
                    max_block = chunk.blocks.back().point();
                }
            }
            if (!cr.empty()) {
                if (max_epoch)
                    logger::info("revalidating up to and including epoch: {}", *max_epoch);
                // The replay transaction truncates derived state at genesis.
                // Do not manufacture an inconsistent registry before opening it.
                chunk_processor progress_proc {
                    .on_progress = [](const auto name, const auto rel_pos, const auto rel_target) {
                        progress::get().update(std::string { name }, rel_pos, rel_target);
                    }
                };
                cr.register_processor(progress_proc);
                const scope_exit progress_proc_cleanup { [&] {
                    cr.remove_processor(progress_proc);
                } };
                cr.validation(validation_mode);
                cr.revalidate(max_block);
                if (max_epoch)
                    cr.remover().remove();
                if (!cr.chunks().empty()) {
                    const auto &last_chunk = cr.chunks().rbegin()->second;
                    logger::info("validation complete last_slot: {} last_block: {} took: {:0.1f} secs",
                        last_chunk.last_block.slot, last_chunk.last_block.hash, t.stop(false));
                } else {
                    logger::info("validation complete with an empty chain took: {:0.1f} secs", t.stop(false));
                }
            } else {
                throw error("chunk_registry is empty - nothing to validate!");
            }
        }
    private:
        using chunk_registry = turbo::chunk_registry;
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}
