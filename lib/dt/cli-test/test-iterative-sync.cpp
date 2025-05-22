/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/cbor/compare.hpp>
#include <dt/cli.hpp>
#include <dt/sync/p2p.hpp>

namespace daedalus_turbo::cli::test_iterative_sync {
    using namespace cardano::ledger;

    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "test-iterative-sync";
            cmd.desc = "test the transactional storage by iteratively issuing truncate and syncs operations";
            cmd.args.expect({ "<data-dir>" });
        }

        void run(const arguments &args) const override
        {
            const auto &data_dir = args.at(0);
            chunk_registry cr { data_dir };
            sync::p2p::syncer syncr { cr };
            for (size_t epoch_to = 208; epoch_to < 228; ++epoch_to) {
                const auto slot_to = cardano::slot::from_epoch(epoch_to, cr.config());
                if (const auto tip = cr.tip(); tip && tip->slot > slot_to) {
                    cardano::optional_point truncate_to {};
                    if (const auto blk = cr.latest_block_before_or_at_slot(slot_to); blk)
                        truncate_to = blk->point();
                    logger::info("truncate to {}", truncate_to);
                    cr.truncate(truncate_to);
                }
                syncr.sync(syncr.find_peer(), slot_to);
            }
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}