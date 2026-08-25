/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/chunk-registry.hpp>
#include <turbo/common/scope-exit.hpp>
#include <turbo/sync/base.hpp>
#include <turbo/txwit/validator.hpp>
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
            cardano::optional_point max_block {};
            chunk_list chunks {};
            bool has_data = false;
            {
                chunk_registry cr { data_dir, chunk_registry::mode::store };
                has_data = !cr.empty();
                for (const auto &[offset, chunk]: cr.chunks()) {
                    if (max_epoch && cr.make_slot(chunk.last_slot).epoch() > *max_epoch)
                        continue;
                    max_block = chunk.blocks.back().point();
                    chunks.emplace_back(chunk);
                }
                if (!max_epoch)
                    max_block = cr.tip();
            }
            if (has_data) {
                if (max_epoch)
                    logger::info("revalidating up to and including epoch: {}", *max_epoch);
                // remove all previously prepared indices and validator snapshots
                std::filesystem::remove_all(std::filesystem::path { data_dir } / "index");
                std::filesystem::remove_all(std::filesystem::path { data_dir } / "validate");
                chunk_registry cr { data_dir, chunk_registry::mode::validate, cardano::config::get(),
                    scheduler::get(), file_remover::get(), false };
                chunk_processor progress_proc {
                    .on_progress = [](const auto name, const auto rel_pos, const auto rel_target) {
                        progress::get().update(std::string { name }, rel_pos, rel_target);
                    }
                };
                cr.register_processor(progress_proc);
                const scope_exit progress_proc_cleanup { [&] {
                    cr.remove_processor(progress_proc);
                } };
                _parse_progress_total = max_block ? max_block->end_offset : 0;
                cr.accept_anything_or_throw({}, max_block, [&]{
                    if (!chunks.empty())
                        _validate_chunks(scheduler::get(), cr, std::move(chunks));
                });
                if (max_epoch)
                    cr.remover().remove();
                auto new_local_tip = cr.tip();
                if (new_local_tip && validation_mode != sync::validation_mode_t::none) {
                    timer txwit_timer { fmt::format("{} transaction witness validation", validation_mode), logger::level::info };
                    cardano::optional_point validate_from {};
                    if (validation_mode == sync::validation_mode_t::turbo) {
                        const auto tail = cr.tail_relative_stake();
                        if (!tail.empty() && tail.begin()->second > 0.5)
                            validate_from = tail.begin()->first;
                    }
                    const auto new_valid_tip = txwit::validate(cr, validate_from, new_local_tip, txwit::witness_type::all);
                    logger::debug("the new valid tip: {}", new_valid_tip);
                    if (new_valid_tip != new_local_tip) {
                        cr.truncate(new_valid_tip);
                        new_local_tip = cr.tip();
                    }
                }
                if (!cr.chunks().empty()) {
                    const auto &last_chunk = cr.chunks().rbegin()->second;
                    logger::info("validation complete last_slot: {} last_block: {} took: {:0.1f} secs",
                        last_chunk.last_slot, last_chunk.last_block_hash, t.stop(false));
                } else {
                    logger::info("validation complete with an empty chain took: {:0.1f} secs", t.stop(false));
                }
            } else {
                throw error("chunk_registry is empty - nothing to validate!");
            }
        }
    private:
        using chunk_registry = turbo::chunk_registry;
        using chunk_info = chunk_registry::chunk_info;
        using chunk_list = chunk_registry::chunk_list;

        mutable uint64_t _parse_progress_total {};

        void _validate_chunks(scheduler &sched, chunk_registry &cr, chunk_list &&chunks) const
        {
            timer t { "validate chunks" };
            for (const auto &chunk: chunks) {
                auto save_path = cr.full_path(chunk.rel_path());
                sched.submit("parse", 0 + 100 * (_parse_progress_total - chunk.offset) / _parse_progress_total, [&cr, chunk, save_path]() {
                    try {
                        cr.add_file(chunk.offset, save_path, chunk.compression_level);
                    } catch (std::exception &ex) {
                        std::filesystem::path orig_path { save_path };
                        const auto debug_path = cr.full_path(fmt::format("error/{}", orig_path.filename().string()));
                        logger::warn("moving an unparsable chunk {} to {}", save_path, debug_path);
                        std::filesystem::copy_file(save_path, debug_path, std::filesystem::copy_options::overwrite_existing);
                        throw error(fmt::format("can't parse {}: {}", save_path, ex.what()));
                    }
                });
            }
            sched.process(true);
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}
