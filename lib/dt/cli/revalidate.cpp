/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */
#include <dt/cli.hpp>
#include <dt/chunk-registry.hpp>

namespace daedalus_turbo::cli::validate {
    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "revalidate";
            cmd.desc = "revalidate the blockchain in <data-dir> from scratch";
            cmd.args.expect({ "<data-dir>" });
        }

        void run(const arguments &args) const override
        {
            timer t { "validation", logger::level::trace };
            const auto &data_dir = args.at(0);
            requirements::check(data_dir);
            progress_guard pg { "parse", "merge", "validate" };
            cardano::optional_point max_block {};
            chunk_list chunks {};
            {
                chunk_registry cr { data_dir, chunk_registry::mode::store };
                if (const auto &last_block = cr.last_block(); last_block) {
                    max_block = { last_block->hash, last_block->slot, last_block->height, last_block->end_offset() };
                    for (const auto &[offset, chunk]: cr.chunks()) {
                        chunks.emplace_back(chunk);
                    }
                }
            }
            if (max_block) {
                // remove all previously prepared indices and validator snapshots
                std::filesystem::remove_all(std::filesystem::path { data_dir } / "index");
                std::filesystem::remove_all(std::filesystem::path { data_dir } / "validate");
                chunk_registry cr { data_dir, chunk_registry::mode::validate, cardano::config::get(),
                    scheduler::get(), file_remover::get(), false };
                _parse_progress.total = max_block->end_offset;
                cr.accept_anything_or_throw({}, max_block, [&]{
                    _validate_chunks(scheduler::get(), cr, std::move(chunks));
                });
                if (!cr.chunks().empty()) {
                    const auto &last_chunk = cr.chunks().rbegin()->second;
                    logger::info("validation complete last_slot: {} last_block: {} took: {:0.1f} secs",
                        last_chunk.last_slot, last_chunk.last_block_hash, t.stop(false));
                }
            } else {
                throw error("chunk_registry is empty - nothing to validate!");
            }
        }
    private:
        using chunk_registry = daedalus_turbo::chunk_registry;
        using chunk_info = chunk_registry::chunk_info;
        using chunk_list = chunk_registry::chunk_list;

        mutable progress::info _parse_progress {};

        void _validate_chunks(scheduler &sched, chunk_registry &cr, chunk_list &&chunks) const
        {
            timer t { "validate chunks" };
            for (const auto &chunk: chunks) {
                auto save_path = cr.full_path(chunk.rel_path());
                sched.submit_void("parse", 0 + 100 * (_parse_progress.total - chunk.offset) / _parse_progress.total, [&cr, chunk, save_path]() {
                    try {
                        cr.add(chunk.offset, save_path);
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