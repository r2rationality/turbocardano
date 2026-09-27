/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Utxo.
// Included at class scope by lib/turbo/txwit/validator.cpp.

            void _validate_reference_scripts(batch_info &part) const
            {
                timer t { fmt::format("txwit batch: {} epoch: {} validate reference scripts", part.part_id, part.epoch), logger::level::debug };
                static constexpr size_t max_ref_script_size_per_tx = 200 * 1024;
                static constexpr size_t max_ref_script_size_per_block = 1024 * 1024;
                part.ref_script_sizes.clear();
                const auto protocol_major = _st.params().protocol_ver.major;
                if (protocol_major < 9)
                    return;
                if (part.ref_script_parts.size() != batch_info::num_parts) [[unlikely]]
                    throw error(fmt::format(
                        "invalid number of reference-script join partitions: {} (expected: {})",
                        part.ref_script_parts.size(), batch_info::num_parts));
                size_t num_uses = 0;
                size_t num_produced = 0;
                size_t num_consumed = 0;
                for (const auto &ref_part: part.ref_script_parts) {
                    num_uses += ref_part.uses.size();
                    num_produced += ref_part.produced.size();
                    num_consumed += ref_part.consumed.size();
                }
                const auto rss_before_mb = memory::my_usage_mb();

                // A TXO can be referenced from several hash partitions, so each partition
                // contributes independently to the transaction totals. The batch-local
                // producer position supplies the ordering that a serial UTXO replay used to
                // provide. Existing ledger TXOs precede every position in this batch.
                const auto num_txs = part.ref_script_txs.size();
                part.ref_script_sizes.resize(num_txs);
                auto ledger_script_sizes = std::make_unique<std::atomic_size_t[]>(num_txs);
                auto block_script_sizes = std::make_unique<std::atomic_size_t[]>(num_txs);
                for (size_t ti = 0; ti < num_txs; ++ti) {
                    ledger_script_sizes[ti].store(0, std::memory_order_relaxed);
                    block_script_sizes[ti].store(0, std::memory_order_relaxed);
                }

                static const std::string task_id { "validate-reference-scripts" };
                auto &sched = _cr.sched();
                const auto join_start = diagnostic_clock::now();
                sched.wait_all(task_id, [&](const auto &, const auto &submit_f) {
                    for (size_t pi = 0; pi < batch_info::num_parts; ++pi) {
                        if (part.ref_script_parts[pi].uses.empty())
                            continue;
                        submit_f({ 2000, task_id, [&, pi] {
                            const auto &ref_part = part.ref_script_parts[pi];
                            const auto &utxo_part = _st.utxos().partition(pi);
                            for (const auto &use: ref_part.uses) {
                                if (use.tx_info_idx >= num_txs) [[unlikely]]
                                    throw error(fmt::format(
                                        "invalid reference-script transaction index: {} (number of transactions: {})",
                                        use.tx_info_idx, num_txs));
                                const auto &tx = part.ref_script_txs[use.tx_info_idx];

                                const auto produced_it = std::lower_bound(
                                    ref_part.produced.begin(), ref_part.produced.end(), use.id,
                                    [](const ref_script_production_t &entry, const tx_out_ref &id) {
                                        return entry.id < id;
                                    });
                                const auto *produced = produced_it != ref_part.produced.end() && produced_it->id == use.id
                                    ? &*produced_it
                                    : nullptr;
                                const auto consumed_it = std::lower_bound(
                                    ref_part.consumed.begin(), ref_part.consumed.end(), use.id,
                                    [](const ref_script_consumption_t &entry, const tx_out_ref &id) {
                                        return entry.id < id;
                                    });
                                const auto *consumed = consumed_it != ref_part.consumed.end() && consumed_it->id == use.id
                                    ? &*consumed_it
                                    : nullptr;
                                const auto ledger_it = utxo_part.find(use.id);
                                const auto *ledger_txo = ledger_it != utxo_part.end()
                                    ? &ledger_it->second
                                    : nullptr;

                                // A collision with an existing UTXO would make a single
                                // producer index ambiguous and is invalid independently of
                                // reference-script accounting.
                                if (produced && ledger_txo) [[unlikely]]
                                    throw error(fmt::format(
                                        "reference-script accounting found duplicate TXO {}",
                                        use.id));

                                // Input availability is shared by spending, reference and
                                // collateral uses. Only the first two charge reference scripts.
                                const bool produced_before = produced && produced->pos < tx.pos;
                                const bool spent_before = consumed && consumed->pos < tx.pos;
                                if ((!produced_before && !ledger_txo) || spent_before) [[unlikely]] {
                                    throw error(fmt::format(
                                        "reference-script accounting references an unavailable TXO {}",
                                        use.id));
                                }
                                if (!use.count_script)
                                    continue;

                                size_t ledger_script_size = 0;
                                if (produced_before)
                                    ledger_script_size = produced->script_size;
                                else if (ledger_txo->script_ref)
                                    ledger_script_size = ledger_txo->script_ref->script().size();

                                size_t block_script_size = 0;
                                if (produced) {
                                    // BBODY <= PV10 sees the UTXO at block start. PV11 also
                                    // sees scripts produced by an earlier transaction in the
                                    // current block. A same-block spend does not remove an
                                    // item from that accounting view.
                                    if (produced->pos.block_height < tx.pos.block_height) {
                                        if (!consumed || consumed->pos.block_height >= tx.pos.block_height)
                                            block_script_size = produced->script_size;
                                    } else if (protocol_major >= 11
                                            && produced->pos.block_height == tx.pos.block_height
                                            && produced->pos.tx_idx < tx.pos.tx_idx) {
                                        block_script_size = produced->script_size;
                                    }
                                } else if (ledger_txo) {
                                    if (!consumed || consumed->pos.block_height >= tx.pos.block_height) {
                                        if (ledger_txo->script_ref)
                                            block_script_size = ledger_txo->script_ref->script().size();
                                    }
                                }

                                ledger_script_sizes[use.tx_info_idx].fetch_add(
                                    ledger_script_size, std::memory_order_relaxed);
                                block_script_sizes[use.tx_info_idx].fetch_add(
                                    block_script_size, std::memory_order_relaxed);
                            }
                        }});
                    }
                });
                const auto join_ns = _diagnostic_elapsed_ns(join_start);

                const auto reduce_start = diagnostic_clock::now();
                timer reduce_timer { fmt::format("txwit batch: {} epoch: {} seq reduce reference scripts", part.part_id, part.epoch), logger::level::debug };
                for (const auto &block: part.ref_script_blocks) {
                    const auto block_tx_end = static_cast<size_t>(block.first_tx_idx) + block.num_txs;
                    if (block_tx_end > num_txs) [[unlikely]]
                        throw error(fmt::format(
                            "invalid reference-script block transaction range: {}..{} (number of transactions: {})",
                            block.first_tx_idx, block_tx_end, num_txs));
                    size_t block_script_size = 0;
                    for (size_t ti = block.first_tx_idx; ti < block_tx_end; ++ti) {
                        const auto &tx = part.ref_script_txs[ti];
                        if (tx.pos.block_height != block.height) [[unlikely]]
                            throw error(fmt::format(
                                "reference-script transaction height {} does not match block height {}",
                                tx.pos.block_height, block.height));
                        if (tx.regular_ref_overlap) [[unlikely]] {
                            throw error(fmt::format(
                                "slot {} tx {} uses TXO {} as both a regular and a reference input",
                                block.slot,
                                tx.tx_id,
                                *tx.regular_ref_overlap));
                        }
                        const auto ledger_tx_script_size = ledger_script_sizes[ti].load(std::memory_order_relaxed);
                        if (ledger_tx_script_size > max_ref_script_size_per_tx) [[unlikely]] {
                            throw error(fmt::format(
                                "slot {} tx {} reference scripts have size {} exceeding the per-transaction limit {}",
                                block.slot,
                                tx.tx_id,
                                ledger_tx_script_size,
                                max_ref_script_size_per_tx));
                        }
                        block_script_size += block_script_sizes[ti].load(std::memory_order_relaxed);
                        part.ref_script_sizes[ti] = ledger_tx_script_size;
                    }
                    if (block_script_size > max_ref_script_size_per_block) [[unlikely]] {
                        throw error(fmt::format(
                            "slot {} reference scripts have total size {} exceeding the per-block limit {}",
                            block.slot,
                            block_script_size,
                            max_ref_script_size_per_block));
                    }
                }
                logger::debug(
                    "txwit reference-script diagnostics batch: {} epoch: {} blocks: {} txs: {} uses: {} produced: {} consumed: {} "
                    "join_ms: {:.3f} reduce_ms: {:.3f} rss_before_mb: {} rss_after_mb: {} peak_rss_mb: {}",
                    part.part_id, part.epoch, part.ref_script_blocks.size(), part.ref_script_txs.size(),
                    num_uses, num_produced, num_consumed, _diagnostic_ms(join_ns),
                    _diagnostic_ms(_diagnostic_elapsed_ns(reduce_start)), rss_before_mb,
                    memory::my_usage_mb(), memory::max_usage_mb());
            }
