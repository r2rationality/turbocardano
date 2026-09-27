/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Utxo.
// Included at namespace scope by lib/turbo/cardano/ledger/shelley.cpp.

    void state::utxo_add(const cardano::tx_out_ref &txo_id, cardano::tx_out_data &&txo_data)
    {
        if (!txo_data.empty()) [[likely]] {
            auto [it, created] = _utxo.try_emplace(txo_id, std::move(txo_data));
            if (!created)
                logger::warn("a non-unique TXO {}!", it->first);
        }
    }

    void state::utxo_del(const cardano::tx_out_ref &txo_id)
    {
        if (const size_t num_del = _utxo.erase(txo_id); num_del != 1) [[unlikely]]
            throw error(fmt::format("request to remove an unknown TXO {}!", txo_id));
    }

    void state::_process_utxo_updates(utxo_update_list &&utxo_updates)
    {
        std::vector<size_t> active_parts;
        for (size_t pi = 0; pi < txo_map::num_parts; ++pi)
            if (std::ranges::any_of(utxo_updates, [pi](const auto &batch) { return !batch.partition(pi).empty(); }))
                active_parts.push_back(pi);
        if (active_parts.empty())
            return;
        using account_map = partitioned_map<stake_ident, account_info>;
        constexpr size_t num_account_parts = account_map::num_parts;
        using diagnostic_clock = std::chrono::steady_clock;
        struct utxo_partition_diagnostics {
            uint64_t elapsed_ns = 0;
            size_t updates = 0;
            size_t stake_deltas = 0;
            size_t pointer_deltas = 0;
        };
        struct stake_partition_diagnostics {
            uint64_t elapsed_ns = 0;
            size_t partial_deltas = 0;
            size_t unique_deltas = 0;
        };
        struct destroy_diagnostics {
            uint64_t elapsed_ns = 0;
            size_t entries = 0;
        };
        struct partitioned_stake_deltas {
            std::vector<std::pair<stake_ident, int64_t>> values {};
            std::array<size_t, account_map::num_parts + 1> offsets {};
        };
        using pool_delta_map = std::map<pool_hash, int64_t>;
        const auto elapsed_ns = [](const diagnostic_clock::time_point start) {
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(diagnostic_clock::now() - start).count());
        };

        const std::string utxo_task_group = fmt::format("ledger-state:apply-utxo-updates:epoch-{}", _epoch);
        const std::string stake_task_group = fmt::format("ledger-state:apply-stake-deltas:epoch-{}", _epoch);
        std::array<partitioned_stake_deltas, txo_map::num_parts> stake_deltas_by_source {};
        std::array<pointer_update_map, txo_map::num_parts> pointer_deltas_by_source {};
        std::array<pool_delta_map, num_account_parts> pool_deltas_by_account_part {};
        std::array<utxo_partition_diagnostics, txo_map::num_parts> utxo_part_diag {};
        std::array<stake_partition_diagnostics, num_account_parts> stake_part_diag {};
        const auto total_start = diagnostic_clock::now();
        const auto utxo_phase_start = diagnostic_clock::now();
        {
            turbo::timer t { fmt::format("validator epoch {} apply utxo partitions batches: {}", _epoch, utxo_updates.size()), logger::level::trace };
            _sched.wait_all(utxo_task_group,
                [&](const auto &todo, const auto &submit_f) {
                    for (const auto part_idx: active_parts) {
                        submit_f({ 1000, utxo_task_group, [this, part_idx, todo, &utxo_updates, &stake_deltas_by_source,
                                &pointer_deltas_by_source, &utxo_part_diag, &elapsed_ns] {
                            const auto task_start = diagnostic_clock::now();
                            size_t num_updates = 0;
                            stake_update_map deltas {};
                            pointer_update_map pointer_deltas {};
                            for (auto &&update_batch: utxo_updates) {
                                auto &upd_part = update_batch.partition(part_idx);
                                num_updates += upd_part.size();
                                auto &utxo_part = _utxo.partition(part_idx);
                                for (auto &&[txo_id, txo_data]: upd_part) {
                                    if (!txo_data.address_raw.empty()) {
                                        const auto addr = txo_data.addr();
                                        if (addr.has_stake_id()) [[likely]]
                                            _update_stake_delta(deltas, addr.stake_id(), static_cast<int64_t>(txo_data.coin));
                                        else if (addr.has_pointer()) [[unlikely]]
                                            pointer_deltas[addr.pointer()] += static_cast<int64_t>(txo_data.coin);
                                        if (!txo_data.empty()) [[likely]] {
                                            if (auto [it, created] = utxo_part.try_emplace(txo_id, std::move(txo_data)); !created) [[unlikely]]
                                                logger::warn("a non-unique TXO {}!", it->first);
                                        }
                                    } else {
                                        if (auto it = utxo_part.find(txo_id); it != utxo_part.end()) [[likely]] {
                                            const auto addr = it->second.addr();
                                            if (addr.has_stake_id()) [[likely]]
                                                _update_stake_delta(deltas, addr.stake_id(), -static_cast<int64_t>(it->second.coin));
                                            else if (addr.has_pointer()) [[unlikely]]
                                                pointer_deltas[addr.pointer()] -= static_cast<int64_t>(it->second.coin);
                                            utxo_part.erase(it);
                                        } else {
                                            throw error(fmt::format("request to remove an unknown TXO {}!", txo_id));
                                        }
                                    }
                                }
                            }
                            auto &partitioned_deltas = stake_deltas_by_source[part_idx];
                            for (const auto &[stake_id, delta]: deltas)
                                ++partitioned_deltas.offsets[account_map::partition_idx(stake_id) + 1];
                            for (size_t dest_part_idx = 1; dest_part_idx < partitioned_deltas.offsets.size(); ++dest_part_idx)
                                partitioned_deltas.offsets[dest_part_idx] += partitioned_deltas.offsets[dest_part_idx - 1];
                            auto positions = partitioned_deltas.offsets;
                            partitioned_deltas.values.resize(deltas.size());
                            for (const auto &[stake_id, delta]: deltas) {
                                const auto dest_part_idx = account_map::partition_idx(stake_id);
                                partitioned_deltas.values[positions[dest_part_idx]++] = { stake_id, delta };
                            }
                            const auto num_pointer_deltas = pointer_deltas.size();
                            pointer_deltas_by_source[part_idx] = std::move(pointer_deltas);
                            utxo_part_diag[part_idx] = {
                                .elapsed_ns=elapsed_ns(task_start),
                                .updates=num_updates,
                                .stake_deltas=partitioned_deltas.values.size(),
                                .pointer_deltas=num_pointer_deltas
                            };
                        }});
                    }
                });
        }
        const auto utxo_phase_ns = elapsed_ns(utxo_phase_start);
        const auto num_utxo_update_batches = utxo_updates.size();
        std::vector<destroy_diagnostics> destroy_diag(num_utxo_update_batches);
        const auto stake_phase_start = diagnostic_clock::now();
        {
            turbo::timer t { fmt::format("validator epoch {} apply stake delta partitions and destroy utxo update maps", _epoch),
                logger::level::trace };
            _sched.wait_all(stake_task_group,
                [&](const auto &todo, const auto &submit_f) {
                    for (size_t part_idx = 0; part_idx < num_account_parts; ++part_idx) {
                        size_t num_partial_deltas = 0;
                        for (const auto source: active_parts)
                            num_partial_deltas += stake_deltas_by_source[source].offsets[part_idx + 1]
                                - stake_deltas_by_source[source].offsets[part_idx];
                        if (!num_partial_deltas)
                            continue;
                        submit_f({ 1000, stake_task_group, [this, part_idx, todo, num_partial_deltas, &active_parts, &stake_deltas_by_source,
                                &pool_deltas_by_account_part, &stake_part_diag, &elapsed_ns] {
                            const auto task_start = diagnostic_clock::now();
                            stake_update_map deltas {};
                            deltas.reserve(num_partial_deltas);
                            for (const auto source: active_parts) {
                                const auto &source_deltas = stake_deltas_by_source[source];
                                for (size_t delta_idx = source_deltas.offsets[part_idx];
                                        delta_idx < source_deltas.offsets[part_idx + 1]; ++delta_idx) {
                                    const auto &[stake_id, delta] = source_deltas.values[delta_idx];
                                    _update_stake_delta(deltas, stake_id, delta);
                                }
                            }
                            auto &account_part = _accounts.partition(part_idx);
                            auto &pool_deltas = pool_deltas_by_account_part[part_idx];
                            const auto num_unique_deltas = deltas.size();
                            for (const auto &[stake_id, delta]: deltas) {
                                auto &acc = account_part[stake_id];
                                if (delta >= 0) {
                                    acc.stake += static_cast<uint64_t>(delta);
                                } else {
                                    const uint64_t dec = static_cast<uint64_t>(-delta);
                                    if (acc.stake < dec) [[unlikely]] {
                                        throw error(fmt::format("trying to remove from account {} more stake {} than it has: {}",
                                            stake_id, dec, acc.stake));
                                    }
                                    acc.stake -= dec;
                                }
                                if (acc.deleg && _active_pool_params.contains(*acc.deleg)) {
                                    const auto [it, created] = pool_deltas.try_emplace(*acc.deleg, delta);
                                    if (!created) {
                                        it->second += delta;
                                        if (!it->second)
                                            pool_deltas.erase(it);
                                    }
                                }
                            }
                            stake_part_diag[part_idx] = {
                                .elapsed_ns=elapsed_ns(task_start),
                                .partial_deltas=num_partial_deltas,
                                .unique_deltas=num_unique_deltas
                            };
                        }});
                    }
                    // The preceding UTXO-partition barrier is the last reader of these maps.
                    // Retire them alongside the stake-delta tasks so both operations share one wait cycle.
                    for (size_t batch_idx = 0; batch_idx < num_utxo_update_batches; ++batch_idx) {
                        submit_f({ 1000, stake_task_group, [batch_idx, todo, &utxo_updates, &destroy_diag, &elapsed_ns] {
                            const auto task_start = diagnostic_clock::now();
                            const auto num_entries = utxo_updates[batch_idx].size();
                            for (size_t pi = 0; pi < txo_map::num_parts; ++pi)
                                if (!utxo_updates[batch_idx].partition(pi).empty())
                                    utxo_updates[batch_idx].clear_partition(pi);
                            destroy_diag[batch_idx] = {
                                .elapsed_ns=elapsed_ns(task_start),
                                .entries=num_entries
                            };
                        }});
                    }
                });
        }
        const auto stake_phase_ns = elapsed_ns(stake_phase_start);
        const auto cleanup_start = diagnostic_clock::now();
        utxo_updates.clear();
        const auto cleanup_ns = elapsed_ns(cleanup_start);
        size_t pool_partial_entries = 0;
        size_t pool_merged_entries = 0;
        size_t pointer_partial_entries = 0;
        size_t pointer_merged_entries = 0;
        uint64_t pool_merge_ns = 0;
        uint64_t pointer_merge_ns = 0;
        {
            const auto pool_merge_start = diagnostic_clock::now();
            pool_delta_map all_pool_deltas {};
            for (const auto &pool_deltas: pool_deltas_by_account_part) {
                pool_partial_entries += pool_deltas.size();
                for (const auto &[pool_id, delta]: pool_deltas) {
                    const auto [it, created] = all_pool_deltas.try_emplace(pool_id, delta);
                    if (!created) {
                        it->second += delta;
                        if (!it->second)
                            all_pool_deltas.erase(it);
                    }
                }
            }
            for (const auto &[pool_id, delta]: all_pool_deltas) {
                if (delta >= 0)
                    _active_pool_dist.add(pool_id, static_cast<uint64_t>(delta));
                else
                    _active_pool_dist.sub(pool_id, static_cast<uint64_t>(-delta));
            }
            pool_merged_entries = all_pool_deltas.size();
            pool_merge_ns = elapsed_ns(pool_merge_start);

            const auto pointer_merge_start = diagnostic_clock::now();
            pointer_update_map all_pointer_deltas {};
            for (const auto &pointer_deltas: pointer_deltas_by_source) {
                pointer_partial_entries += pointer_deltas.size();
                for (const auto &[stake_ptr, delta]: pointer_deltas) {
                    const auto [it, created] = all_pointer_deltas.try_emplace(stake_ptr, delta);
                    if (!created) {
                        it->second += delta;
                        if (!it->second)
                            all_pointer_deltas.erase(it);
                    }
                }
            }
            for (const auto &[stake_ptr, delta]: all_pointer_deltas)
                update_pointer(stake_ptr, delta);
            pointer_merged_entries = all_pointer_deltas.size();
            pointer_merge_ns = elapsed_ns(pointer_merge_start);
        }

        uint64_t utxo_task_sum_ns = 0;
        uint64_t utxo_task_max_ns = 0;
        size_t utxo_task_max_part = 0;
        size_t total_updates = 0;
        size_t total_stake_deltas = 0;
        size_t total_pointer_deltas = 0;
        for (size_t part_idx = 0; part_idx < utxo_part_diag.size(); ++part_idx) {
            const auto &diag = utxo_part_diag[part_idx];
            utxo_task_sum_ns += diag.elapsed_ns;
            total_updates += diag.updates;
            total_stake_deltas += diag.stake_deltas;
            total_pointer_deltas += diag.pointer_deltas;
            if (diag.elapsed_ns > utxo_task_max_ns) {
                utxo_task_max_ns = diag.elapsed_ns;
                utxo_task_max_part = part_idx;
            }
        }
        uint64_t stake_task_sum_ns = 0;
        uint64_t stake_task_max_ns = 0;
        size_t stake_task_max_part = 0;
        size_t total_partial_deltas = 0;
        size_t total_unique_deltas = 0;
        for (size_t part_idx = 0; part_idx < stake_part_diag.size(); ++part_idx) {
            const auto &diag = stake_part_diag[part_idx];
            stake_task_sum_ns += diag.elapsed_ns;
            total_partial_deltas += diag.partial_deltas;
            total_unique_deltas += diag.unique_deltas;
            if (diag.elapsed_ns > stake_task_max_ns) {
                stake_task_max_ns = diag.elapsed_ns;
                stake_task_max_part = part_idx;
            }
        }
        uint64_t destroy_task_sum_ns = 0;
        uint64_t destroy_task_max_ns = 0;
        size_t destroy_task_max_batch = 0;
        for (size_t batch_idx = 0; batch_idx < destroy_diag.size(); ++batch_idx) {
            const auto &diag = destroy_diag[batch_idx];
            destroy_task_sum_ns += diag.elapsed_ns;
            if (diag.elapsed_ns > destroy_task_max_ns) {
                destroy_task_max_ns = diag.elapsed_ns;
                destroy_task_max_batch = batch_idx;
            }
        }
        logger::debug(
            "ledger UTXO diagnostics epoch: {} batches: {} updates: {} "
            "utxo_wall_ms: {:.3f} utxo_task_sum_ms: {:.3f} utxo_task_max_ms: {:.3f} utxo_task_max_part: {} "
            "utxo_task_max_updates: {} utxo_task_max_stake_deltas: {} utxo_task_max_pointer_deltas: {} "
            "stake_deltas: {} pointer_deltas: {} "
            "stake_wall_ms: {:.3f} stake_task_sum_ms: {:.3f} stake_task_max_ms: {:.3f} stake_task_max_part: {} "
            "stake_task_max_partial_deltas: {} stake_task_max_unique_deltas: {} partial_deltas: {} unique_deltas: {} "
            "destroy_task_sum_ms: {:.3f} destroy_task_max_ms: {:.3f} destroy_task_max_batch: {} destroy_task_max_entries: {} "
            "cleanup_ms: {:.3f} pool_merge_ms: {:.3f} pool_partial_entries: {} pool_merged_entries: {} "
            "pointer_merge_ms: {:.3f} pointer_partial_entries: {} pointer_merged_entries: {} total_ms: {:.3f}",
            _epoch, num_utxo_update_batches, total_updates,
            static_cast<double>(utxo_phase_ns) / 1'000'000,
            static_cast<double>(utxo_task_sum_ns) / 1'000'000,
            static_cast<double>(utxo_task_max_ns) / 1'000'000,
            utxo_task_max_part, utxo_part_diag[utxo_task_max_part].updates,
            utxo_part_diag[utxo_task_max_part].stake_deltas,
            utxo_part_diag[utxo_task_max_part].pointer_deltas,
            total_stake_deltas, total_pointer_deltas,
            static_cast<double>(stake_phase_ns) / 1'000'000,
            static_cast<double>(stake_task_sum_ns) / 1'000'000,
            static_cast<double>(stake_task_max_ns) / 1'000'000,
            stake_task_max_part, stake_part_diag[stake_task_max_part].partial_deltas,
            stake_part_diag[stake_task_max_part].unique_deltas,
            total_partial_deltas, total_unique_deltas,
            static_cast<double>(destroy_task_sum_ns) / 1'000'000,
            static_cast<double>(destroy_task_max_ns) / 1'000'000,
            destroy_task_max_batch,
            destroy_diag.empty() ? 0 : destroy_diag[destroy_task_max_batch].entries,
            static_cast<double>(cleanup_ns) / 1'000'000,
            static_cast<double>(pool_merge_ns) / 1'000'000,
            pool_partial_entries, pool_merged_entries,
            static_cast<double>(pointer_merge_ns) / 1'000'000,
            pointer_partial_entries, pointer_merged_entries,
            static_cast<double>(elapsed_ns(total_start)) / 1'000'000);
    }

    void state::_process_collateral_use(tx_out_ref_list &&collected_collateral)
    {
        for (const auto &txo_id: collected_collateral) {
            const auto txo_data = utxo_find(txo_id);
            if (!txo_data) [[unlikely]]
                throw error(fmt::format("epoch {}: cannot find data about a TXO used as a collateral input: {}", _epoch, txo_id));
            add_fees(txo_data->coin);
            if (const auto addr = txo_data->addr(); addr.has_stake_id_hybrid()) [[likely]]
                update_stake_id_hybrid(addr.stake_id_hybrid(), -static_cast<int64_t>(txo_data->coin));
            utxo_del(txo_id);
        }
    }

    void state::update_stake(const stake_ident &stake_id, const int64_t delta)
    {
        auto &acc = _accounts[stake_id];
        if (delta >= 0) {
            acc.stake += static_cast<uint64_t>(delta);
            if (acc.deleg && _active_pool_params.contains(*acc.deleg))
                _active_pool_dist.add(*acc.deleg, static_cast<uint64_t>(delta));
        } else {
            const uint64_t dec = static_cast<uint64_t>(-delta);
            if (acc.stake < dec) [[unlikely]]
                throw error(fmt::format("trying to remove from account {} more stake {} than it has: {}", stake_id, dec, acc.stake));
            acc.stake -= dec;
            if (acc.deleg && _active_pool_params.contains(*acc.deleg))
                _active_pool_dist.sub(*acc.deleg, static_cast<uint64_t>(-delta));
        }
    }

    void state::update_pointer(const cardano::stake_pointer &ptr, const int64_t delta)
    {
        /*if (const auto ptr_it = _ptr_to_stake.find(ptr); ptr_it != _ptr_to_stake.end()) {
            logger::trace("epoch: {} stake update via pointer: {} {} delta: {}", _epoch, ptr, ptr_it->second, cardano::balance_change { delta });
            update_stake(ptr_it->second, delta);
        } else { */
        if (delta) {
            if (delta >= 0)
                _stake_pointers.add(ptr, delta);
            else if (_stake_pointers.contains(ptr))
                _stake_pointers.sub(ptr, static_cast<uint64_t>(-delta));
            else
                logger::warn("epoch: {} skipping an unknown stake pointer: {} delta: {}", _epoch, ptr, cardano::balance_change { delta });
        }
    }

    void state::update_stake_id_hybrid(const cardano::stake_ident_hybrid &stake_id, const int64_t delta)
    {
        if (delta) {
            if (std::holds_alternative<cardano::stake_ident>(stake_id))
                update_stake(std::get<cardano::stake_ident>(stake_id), delta);
            else if (std::holds_alternative<cardano::stake_pointer>(stake_id))
                update_pointer(std::get<cardano::stake_pointer>(stake_id), delta);
            else
                throw error("internal error: an unexpected value for a stake_indent!");
        }
    }

    void state::sub_fees(const uint64_t refund)
    {
        if (_fees_next_reward >= refund) [[likely]]
            _fees_next_reward -= refund;
        else
            throw error(fmt::format("insufficient fees_next_reward: {} to refund {}", _fees_next_reward, refund));
        if (_fees_utxo >= refund) [[likely]]
            _fees_utxo -= refund;
        else
            throw error(fmt::format("insufficient fees_utxo: {} to refund {}", _fees_utxo, refund));
    }

    void state::add_fees(const uint64_t amount)
    {
        _fees_next_reward += amount;
        _fees_utxo += amount;
    }
