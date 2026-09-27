/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Ledger.
// Included at class scope by lib/turbo/txwit/validator.cpp.

            update_effects_t _apply_ledger_updates_before_witnesses(batch_info &part)
            {
                timer t { fmt::format("txwit batch: {} epoch: {} seq apply ledger updates before witnesses", part.part_id, part.epoch), logger::level::debug };
                const auto rss_before_mb = memory::my_usage_mb();
                const auto num_block_updates = part.block_updates.size();
                const auto num_timed_updates = part.timed_updates.size();
                const auto last_slot = part.block_updates.empty()
                    ? optional_slot {} : optional_slot { part.block_updates.back().slot };
                block_update_list block_updates {};
                block_updates.reserve(part.block_updates.size());
                for (auto &&upd: part.block_updates)
                    block_updates.emplace_back(std::move(upd));
                auto stage_start = diagnostic_clock::now();
                _st.process_block_updates(std::move(block_updates));
                const auto block_updates_ns = _diagnostic_elapsed_ns(stage_start);

                update_effects_t effects {};
                stage_start = diagnostic_clock::now();
                cardano::ledger::rules::foreach_transaction(part.timed_updates,
                    [](const auto &upd) { return upd.update.loc; }, [&](auto transaction) {
                        for (auto &upd: transaction) {
                            _observe_deposit_effects(part, upd);
                            if (upd.apply)
                                _st.process_timed_update(effects, std::move(upd.update));
                        }
                        _st.finish_transaction();
                    });
                if (part.timed_updates.empty())
                    _st.finish_transaction();
                if (last_slot)
                    _st.tick(*last_slot);
                const auto timed_updates_ns = _diagnostic_elapsed_ns(stage_start);
                logger::debug(
                    "txwit pre-ledger diagnostics batch: {} epoch: {} block_updates: {} timed_updates: {} "
                    "block_updates_ms: {:.3f} timed_updates_ms: {:.3f} rss_before_mb: {} rss_after_mb: {} peak_rss_mb: {}",
                    part.part_id, part.epoch, num_block_updates, num_timed_updates,
                    _diagnostic_ms(block_updates_ns), _diagnostic_ms(timed_updates_ns),
                    rss_before_mb, memory::my_usage_mb(), memory::max_usage_mb());
                return effects;
            }

            void _apply_ledger_updates_after_witnesses(batch_info &part, update_effects_t &&effects)
            {
                if (part.collateral_fees.high)
                    throw error("collateral fees exceed the coin representation");
                timer t { fmt::format("txwit batch: {} epoch: {} par apply ledger updates after witnesses", part.part_id, part.epoch), logger::level::debug };
                const auto rss_before_mb = memory::my_usage_mb();
                const auto num_utxo_updates = part.utxos.size();
                size_t nonempty_utxo_parts = 0;
                for (size_t pi = 0; pi < txo_map::num_parts; ++pi) {
                    if (!part.utxos.partition(pi).empty())
                        ++nonempty_utxo_parts;
                }
                utxo_update_list utxo_updates {};
                utxo_updates.emplace_back(std::move(part.utxos));
                auto stage_start = diagnostic_clock::now();
                _st.process_utxo_updates(std::move(utxo_updates));
                _st.add_fees(part.collateral_fees.low);
                const auto process_utxos_ns = _diagnostic_elapsed_ns(stage_start);
                stage_start = diagnostic_clock::now();
                _st.finish_update_processing(std::move(effects), part.finalize_after_batch);
                const auto finish_updates_ns = _diagnostic_elapsed_ns(stage_start);
                const auto state_sizes = _st.container_sizes();
                logger::debug(
                    "txwit post-ledger diagnostics batch: {} epoch: {} utxo_updates: {} nonempty_utxo_parts: {} finalize_after_batch: {} "
                    "process_utxos_ms: {:.3f} finish_updates_ms: {:.3f} state_utxos: {} state_accounts: {} state_pointers: {} "
                    "state_snapshot_entries: {} state_reward_entries: {} state_delegation_entries: {} state_pool_entries: {} "
                    "rss_before_mb: {} rss_after_mb: {} peak_rss_mb: {}",
                    part.part_id, part.epoch, num_utxo_updates, nonempty_utxo_parts, part.finalize_after_batch,
                    _diagnostic_ms(process_utxos_ns), _diagnostic_ms(finish_updates_ns),
                    state_sizes.utxos, state_sizes.accounts, state_sizes.pointers,
                    state_sizes.snapshot_entries, state_sizes.reward_entries,
                    state_sizes.delegation_entries, state_sizes.pool_entries,
                    rss_before_mb, memory::my_usage_mb(), memory::max_usage_mb());
            }
