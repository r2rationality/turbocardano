/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <random>
#include <turbo/cardano/ledger/state.hpp>
#include <turbo/chunk-registry.hpp>
#include <turbo/index/merge-zpp.hpp>
#include <turbo/index/utxo.hpp>
#include <turbo/plutus/context.hpp>
#include <turbo/plutus/costs.hpp>
#include <turbo/storage/partition.hpp>
#include "common.hpp"

namespace turbo::cli::sample_blocks {
    using namespace cardano;
    using namespace cardano::ledger;
    using namespace plutus;

    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "sample-blocks";
            cmd.desc = "Extract a random sample of blocks";
            cmd.args.expect({ "<data-dir>", "<sample-path>" });
            cmd.opts.try_emplace("seed", "the random seed", "123456");
            cmd.opts.try_emplace("ratio", "the ratio of blocks to be included", "0.01");
            cmd.opts.try_emplace("from-epoch", "include blocks only after the start of a given epoch", "507");
            cmd.opts.try_emplace("to-epoch", "include blocks only before the end of a given epoch", "525");
        }

        void run(const arguments &args, const options &opts) const override
        {
            file::set_max_open_files();
            const chunk_registry cr { args.at(0), chunk_registry::mode::store };
            const std::filesystem::path out_dir = std::filesystem::weakly_canonical(args.at(1));
            const uint32_t seed = std::stoul(*opts.at("seed"));
            const double ratio = std::stod(*opts.at("ratio"));

            const auto from_slot = slot::from_epoch(std::stoull(*opts.at("from-epoch")), cr.config());
            const auto from = cr.latest_block_after_or_at_slot(from_slot);
            if (from == cr.cend())
                throw error(fmt::format("can't find data for the from_slot: {}", from_slot));

            const auto to_slot = static_cast<uint64_t>(slot::from_epoch(std::stoull(*opts.at("to-epoch")) + 1, cr.config())) - 1;
            const auto to = cr.latest_block_before_or_at_slot(to_slot);
            if (to == cr.cend())
                throw error(fmt::format("can't find data for the to_slot: {}", to_slot));

            storage::partition_map::storage_type batches {};
            for (const auto &[last_byte_off, chunk]: cr.chunks()) {
                if (chunk.last_slot >= from->slot && chunk.first_slot <= to->slot) {
                    storage::partition::storage_type batch {};
                    batch.emplace_back(&chunk);
                    batches.emplace_back(std::move(batch));
                }
            }
            storage::partition_map pm { std::move(batches) };

            mutex::unique_lock::mutex_type all_mutex alignas(mutex::alignment) {};
            part_info all {};

            storage::parse_parallel<part_info>(cr, pm,
                [&](auto &part, const auto &blk) {
                    if (blk->slot() >= from->slot && blk->slot() <= to->slot) {
                        const auto r = part.dist(part.rnd);
                        if (r < ratio) {
                            ++part.num_blocks;
                            // prefix the block by hash to ensure that lexicographical order keeps blocks randomly ordered in practice
                            file::write((out_dir / fmt::format("{}-{}.block", blk->slot(), blk->hash())).string(), blk.raw());
                            blk->foreach_tx([&](auto &) {
                                ++part.num_txs;
                            });
                        }
                    }
                },
                [&](const size_t, const storage::partition &) {
                    return part_info {
                        std::default_random_engine { seed }
                    };
                },
                [&](auto &&part, const size_t, const auto &) {
                    mutex::scoped_lock lk { all_mutex };
                    all.num_blocks += part.num_blocks;
                    all.num_txs += part.num_txs;
                },
                "sample-blocks"
            );

            logger::info("extracted to {}: blocks: {} txs: {}", out_dir.string(), all.num_blocks, all.num_txs);
        }
    private:
        struct part_info {
            std::default_random_engine rnd;
            std::uniform_real_distribution<double> dist {};
            size_t num_blocks = 0;
            size_t num_txs = 0;
        };
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}
