/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cli/common.hpp>
#include <turbo/cbor/compare.hpp>
#include <turbo/sync/p2p.hpp>

namespace turbo::cli::test_ledger_export {
    using namespace cardano::ledger;

    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "test-ledger-export";
            cmd.desc = "generate, export and compare state for a range of epochs";
            cmd.args.expect({ "<data-dir>", "<orig-state-dir>" });
            cmd.opts.try_emplace("host", "the turbo peer to synchronize with");
            cmd.opts.try_emplace("from", "the epoch from which to start the comparisons", "208");
            cmd.opts.try_emplace("to", "the epoch after which to stop the comparisons", "525");
        }

        void run(const arguments &args, const options &opts) const override
        {
            const auto &data_dir = args.at(0);
            const auto &orig_dir = args.at(1);
            chunk_registry cr { data_dir };
            sync::p2p::syncer syncr { cr };
            std::optional<std::string> host {};
            if (const auto opt_it = opts.find("host"); opt_it != opts.end() && opt_it->second)
                host = *opt_it->second;
            auto first_epoch = std::stoull(*opts.at("from"));
            if (first_epoch < 208)
                first_epoch = 208;
            auto last_epoch = std::stoull(*opts.at("to"));
            if (last_epoch < first_epoch)
                last_epoch = first_epoch;
            for (size_t epoch = first_epoch; epoch <= last_epoch; ++epoch) {
                const auto epoch_last_slot = 208 * 21600 + (epoch - 208 + 1) * 432000 - 1;
                struct compare_task {
                    std::string orig_path;
                    std::string gen_path;
                };
                logger::info("testing epoch: {} last slot: {}", epoch, epoch_last_slot);
                std::optional<compare_task> task {};
                {
                    if (cr.max_slot() > epoch_last_slot) {
                        const auto new_tip = cr.epochs().at(epoch).chunks().back()->blocks.back().point();
                        logger::info("truncating the local chain to {}", new_tip);
                        cr.truncate(new_tip);
                    }
                    cr.maintenance();
                    if (const auto tip = cr.tip(); !tip || tip->slot < epoch_last_slot - 200) {
                        std::optional<cardano::network::address> addr {};
                        if (host)
                            addr.emplace(*host, "3001");
                        syncr.sync(syncr.find_peer(addr), epoch_last_slot, sync::validation_mode_t::none);
                    }
                    if (const auto tip = cr.tip(); tip) {
                        task.emplace(fmt::format("{}/{}-{}", orig_dir, epoch, tip->slot), cr.node_export_ledger(data_dir, tip));
                    }
                }
                if (task) {
                    uint8_vector orig_data {}, gen_data {};
                    {
                        timer t1 { "load", logger::level::info };
                        auto &sched = scheduler::get();
                        sched.submit("load-orig", 100, [&] {
                            file::read(task->orig_path, orig_data);
                        });
                        sched.submit("load-gen", 100, [&] {
                            file::read(task->gen_path, gen_data);
                        });
                        sched.process();
                    }
                    timer t2 { "comparison", logger::level::info };
                    const auto diff = cbor::compare(orig_data, gen_data);
                    if (!diff.empty()) [[unlikely]] {
                        throw error(fmt::format("the exported ledger for epoch {} differs: {}", epoch, diff));
                    }
                    logger::info("the exported ledger for epoch {} is the same", epoch);
                    std::filesystem::remove(task->gen_path);
                }
            }
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}
