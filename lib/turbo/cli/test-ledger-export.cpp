/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cli/common.hpp>
#include <turbo/cardano/ledger/node-snapshot.hpp>
#include <turbo/cardano/ledger/state.hpp>
#include <turbo/sync/p2p.hpp>
#include <charconv>
#include <limits>

namespace turbo::cli::test_ledger_export {
    using namespace cardano::ledger;

    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "test-ledger-export";
            cmd.desc = "compare ledger exports with Node 11.1.3 epoch snapshots";
            cmd.args.expect({ "<data-dir>", "<orig-state-dir>" });
            cmd.opts.try_emplace("host", "the turbo peer to synchronize with");
            cmd.opts.try_emplace("from", "first reference epoch (default: all available)");
            cmd.opts.try_emplace("to", "last reference epoch (default: all available)");
        }

        void run(const arguments &args, const options &opts) const override
        {
            const auto &data_dir = args.at(0);
            const auto &orig_dir = args.at(1);
            const auto epoch_option = [&](const std::string &name, uint64_t fallback) {
                const auto it = opts.find(name);
                if (it == opts.end() || !it->second) return fallback;
                const auto &s = *it->second;
                const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), fallback);
                if (ec != std::errc {} || end != s.data() + s.size())
                    throw error{"--{} must be a nonnegative epoch integer", name};
                return fallback;
            };
            const auto refs = node_snapshot::discover(orig_dir, epoch_option("from", 0),
                epoch_option("to", std::numeric_limits<uint64_t>::max()));
            chunk_registry cr { data_dir };
            sync::p2p::syncer syncr { cr };
            std::optional<cardano::network::address> addr;
            if (const auto it = opts.find("host"); it != opts.end() && it->second)
                addr.emplace(*it->second, "3001");
            const auto output_dir = std::filesystem::path { data_dir } / "ledger-export-checks";
            for (const auto &ref: refs) {
                // Inspect just this reference, before any local-chain mutation.
                const node_snapshot::reader expected { ref.path };
                const auto &target = expected.info().tip;
                if (target.slot != ref.slot || expected.info().epoch != ref.epoch)
                    throw error{"{}: snapshot name disagrees with its state", ref.path.string()};
                logger::info("testing epoch {} at slot {} hash {}", ref.epoch, target.slot, target.hash);
                cardano::optional_point point;
                if (cr.tip() && cr.max_slot() >= target.slot) {
                    point = cr.find_block_by_slot(target.slot).point();
                    if (point->slot != target.slot || point->hash != target.hash || point->height != target.height)
                        throw error("reference snapshot does not match the local chain");
                }
                // An exact saved state is sufficient even when the tip is later.
                if (!cr.validator().snapshots().best_exportable(point, true)) {
                    if (point && cr.max_slot() > target.slot) cr.truncate(*point);
                    if (const auto tip = cr.tip(); !tip || tip->slot < target.slot)
                        syncr.sync(syncr.find_peer(addr), target.slot, sync::validation_mode_t::none);
                    point = cr.tip();
                    if (!point || point->slot != target.slot || point->hash != target.hash || point->height != target.height)
                        throw error{"could not reach the reference chain point for epoch {}", ref.epoch};
                    if (!cr.validator().snapshots().best_exportable(point, true)) cr.save_state();
                }
                cr.validator().visit_exportable_state(point, true, [&](auto &state, const auto &tip) {
                    try {
                        const auto encoded = state.to_cbor(tip, 1000, node_format::v11_1_3).flat();
                        expected.compare(encoded, state.utxos());
                    } catch (const std::exception &ex) {
                        const auto generated = output_dir / fmt::format("{}_dt", tip.slot);
                        try {
                            // This command owns the diagnostic directory. Keep only
                            // the latest failure, including across different slots.
                            std::filesystem::remove_all(output_dir);
                            std::filesystem::create_directories(output_dir);
                            state.save_node(generated.string(), tip, 1000, node_format::v11_1_3);
                        } catch (const std::exception &save_error) {
                            throw error{"epoch {} slot {}: {}; could not save diagnostic snapshot at {}: {}",
                                ref.epoch, target.slot, ex.what(), generated.string(), save_error.what()};
                        }
                        throw error{"epoch {} slot {}: {}; generated snapshot retained at {}",
                            ref.epoch, target.slot, ex.what(), generated.string()};
                    }
                });
                logger::info("the exported ledger for epoch {} is the same", ref.epoch);
            }
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}
