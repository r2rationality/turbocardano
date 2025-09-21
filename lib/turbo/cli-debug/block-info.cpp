/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cli/common.hpp>
#include <turbo/cardano.hpp>
#include <turbo/chunk-registry.hpp>

namespace turbo::cli::debug::block_info {
    struct cmd: command {
        const command_info &info() const override
        {
            static const command_info i { "block-info", "<data-dir> <slot>", "print information about the block at a given slot" };
            return i;
        }

        void run(const arguments &args) const override
        {
            if (args.size() < 2) _throw_usage();
            const auto &data_dir = args.at(0);
            const std::string idx_dir = indexer::incremental::storage_dir(data_dir);
            cardano::slot slot { std::stoull(args.at(1)), cardano::config::get() };
            const chunk_registry cr { data_dir, chunk_registry::mode::store };
            const auto &block_meta = cr.find_block_by_slot(slot);
            logger::info("block {} at slot {} has offset {}", block_meta.hash, slot, static_cast<uint64_t>(block_meta.offset));
            //logger::info("chunk: {}", cr.find_chunk_by_slot(slot).rel_path());

        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}