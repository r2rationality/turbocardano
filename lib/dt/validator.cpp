/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/cardano/common/common.hpp>
#include <dt/cardano/ledger/state.hpp>
#include <dt/cardano/ledger/updates.hpp>
#include <dt/chunk-registry.hpp>
#include <dt/container.hpp>
#include <dt/index/block-fees.hpp>
#include <dt/index/timed-update.hpp>
#include <dt/index/utxo.hpp>
#include <dt/index/vrf.hpp>
#include <dt/mutex.hpp>
#include <dt/validator.hpp>
#include <dt/zpp.hpp>

namespace daedalus_turbo::validator {
    using namespace cardano::ledger;

    snapshot snapshot::from_json(const json::value &j)
    {
        return snapshot {
            json::value_to<uint64_t>(j.at("epoch")),
            json::value_to<uint64_t>(j.at("endOffset")),
            json::value_to<uint64_t>(j.at("lastSlot")),
            json::value_to<bool>(j.at("exportable"))
        };
    }

    snapshot::snapshot(const cardano::ledger::state &st)
        : epoch { st.epoch() }, end_offset { st.end_offset() }, last_slot { st.last_slot() }, exportable { st.exportable() }
    {
    }

    snapshot::snapshot(const uint64_t epoch_, const uint64_t end_offset_, const uint64_t last_slot_, const bool exportable_)
        : epoch { epoch_ }, end_offset { end_offset_ }, last_slot { last_slot_ }, exportable { exportable_ }
    {
    }

    json::object snapshot::to_json() const
    {
        return json::object {
            { "epoch", epoch },
            { "endOffset", end_offset },
            { "lastSlot", last_slot },
            { "exportable", exportable }
        };
    }

    snapshot_set::const_iterator snapshot_set::next_excessive() const
    {
        if (size() <= 5)
            return end();
        std::optional<std::pair<const_iterator, uint64_t>> min {};
        // do not consider as excessive the two most recent snapshots
        auto end_it = end();
        --end_it;
        --end_it;
        for (auto it = begin(); it != end_it; ++it) {
            // The score is the number of epochs till the next snapshot.
            // The lower score, the less important the snapshot is.
            // <= is used so that an earlier snapshot with the same score is kept
            if (const auto score = std::next(it)->epoch - it->epoch; !min || score <= min->second)
                min.emplace(it, score);
        }
        if (!min) [[unlikely]]
            throw error("internal error: couldn't identify the least useful snapshot!");
        return min->first;
    }

    void snapshot_set::remove_excessive(const action_t &on_remove, const action_t &on_keep)
    {
        for (auto e_it = next_excessive(); e_it != end(); e_it = next_excessive()) {
            on_remove(*e_it);
            erase(e_it);
        }
        for (auto it = begin(); it != end(); ++it) {
            on_keep(*it);
        }
    }

    const snapshot *snapshot_set::best(const best_predicate_t &pred) const
    {
        for (const auto &snap: *this | std::ranges::views::reverse) {
            if (pred(snap))
                return &snap;
        }
        return nullptr;
    }

    indexer::indexer_map default_indexers(const std::string &data_dir, scheduler &sched)
    {
        const auto idx_dir = indexer::incremental::storage_dir(data_dir);
        auto indexers = indexer::default_list(data_dir, sched);
        indexers.emplace(std::make_shared<index::block_fees::indexer>(idx_dir, "block-fees", sched));
        indexers.emplace(std::make_shared<index::timed_update::indexer>(idx_dir, "timed-update", sched));
        indexers.emplace(std::make_shared<index::vrf::indexer>(idx_dir, "vrf", sched));
        indexers.emplace(std::make_shared<index::utxo::indexer>(idx_dir, "utxo", sched));
        return indexers;
    }

    struct incremental::impl {
        impl(chunk_registry &cr)
        : _cr { cr },
            _validate_dir { chunk_registry::init_db_dir((_cr.data_dir() / "validate").string()) },
            _state_path { (_validate_dir / "state.json").string() },
            _state_pre_path { (_validate_dir / "state-pre.json").string() },
            _state { _cr.config(), _cr.sched() }
        {
            _load_state();
            _cr.register_processor(_proc);
            logger::info("protocol magic: {} byron genesis: {}", _cr.config().byron_protocol_magic, _cr.config().byron_genesis_hash);
        }

        ~impl()
        {
            _cr.remove_processor(_proc);
        }

        void my_truncate(const cardano::optional_point &new_tip, const bool /*track_changes*/)
        {
            timer t { fmt::format("validator::truncate to {}", new_tip), logger::level::debug };
            const auto max_end_offset = new_tip ? new_tip->end_offset : 0;
            if (!_snapshots.empty() && _snapshots.rbegin()->end_offset > max_end_offset) {
                for (auto it = _snapshots.begin(); it != _snapshots.end(); ) {
                    if (it->end_offset > max_end_offset) {
                        for (const auto &prefix: { "ledger" })
                            _cr.remover().mark(_storage_path(prefix, it->end_offset));
                        it = _snapshots.erase(it);
                    } else {
                        ++it;
                    }
                }
                if (!_snapshots.empty()) {
                    const auto &last_snapshot = *_snapshots.rbegin();
                    logger::info("validator's closest snapshot is for epoch {} and end_offset: {}", last_snapshot.epoch, last_snapshot.end_offset);
                    _load_state_snapshot(last_snapshot);
                } else {
                    _state.clear();
                    logger::info("validator has no applicable snapshots, reprocessing the chain data to create one");
                }
                _apply_ledger_updates_fast();
                _remove_temporary_data();
            }
        }

        uint64_t my_end_offset() const
        {
            return _state.end_offset();
        }

        void my_start_tx()
        {
            _next_end_offset = _state.end_offset();
            _next_tasks.clear();
            _reserve_snapshot.reset();
        }

        void my_prepare_tx()
        {
            timer t { "validator::_prepare_tx" };
            // previous validation task must be finished by now
            if (_cr.num_bytes() > _state.end_offset()) {
                mutex::unique_lock lk { _next_task_mutex };
                _schedule_validation(std::move(lk), false);
                _cr.sched().process(true);
            }
            if (_snapshots.empty() || _state.end_offset() > _snapshots.rbegin()->end_offset) {
                _save_state_snapshot();
                _cr.sched().process(true);
            }
            if (_state.valid_end_offset() < _state.end_offset()) {
                throw error(fmt::format("valid subchain end offset: {} is less than the final state end offset: {}",
                    _state.valid_end_offset(), _state.end_offset()));
            }
            if (_reserve_snapshot && _state.valid_end_offset() < _reserve_snapshot->end_offset)
                throw error(fmt::format("valid subchain end offset: {} is less than the reserve state end offset: {}",
                    _state.valid_end_offset(), _reserve_snapshot->end_offset));
        }

        void my_rollback_tx()
        {
            _load_state();
            if (_reserve_snapshot)
                std::filesystem::remove(_storage_path("ledger-reserve", _reserve_snapshot->end_offset));
        }

        void my_commit_tx()
        {
            if (_reserve_snapshot) {
                std::filesystem::rename(_storage_path("ledger-reserve", _reserve_snapshot->end_offset),
                    _storage_path("ledger", _reserve_snapshot->end_offset));
                _snapshots.emplace(*_reserve_snapshot);
            }
            _snapshots.remove_excessive(
                [&](const auto &snap) {
                    const auto snap_path = _storage_path("ledger", snap.end_offset);
                    _cr.remover().mark(snap_path);
                },
                [&](const auto &snap) {
                    const auto snap_path = _storage_path("ledger", snap.end_offset);
                    _cr.remover().unmark(snap_path);
                }
            );
            _save_json_snapshots(_state_path);
            _remove_temporary_data();
        }

        void my_on_epoch_update(const uint64_t epoch, const epoch_info &info)
        {
            // only one thread at a time must work on this
            mutex::unique_lock lk { _next_task_mutex };
            // slice merge notifications may arrive out of order even though they are scheduled in order
            if (info.end_offset() > _next_end_offset)
                _next_end_offset = info.end_offset();
            _next_tasks.try_emplace(epoch, info.first_slot(), info.last_slot());
            _schedule_validation(std::move(lk), false);
        }

        cardano::amount unspent_reward(const cardano::stake_ident &id) const
        {
            return _state.unspent_reward(id);
        }

        cardano::tail_relative_stake_map tail_relative_stake() const
        {
            const auto &stake_dist = _state.pool_stake_dist();
            const auto &genesis_pools = _state.pbft_pools();
            cardano::tail_relative_stake_map tail_stake {};
            set<cardano::pool_hash> seen_pools {};
            double signing_stake = 0.0;
            size_t total_blocks = 0;
            size_t repeated_signer_blocks = 0;
            size_t obsolete_signer_blocks = 0;
            const auto log_stats = [&] {
                logger::debug("tail estimation: total_blocks: {} repeated_signer_blocks: {} obsolete_signer_blocks: {} edge signing stake: {}",
                    total_blocks, repeated_signer_blocks, obsolete_signer_blocks, signing_stake);
            };
            for (const auto &[last_byte_offset, chunk]: _cr.chunks() | std::ranges::views::reverse) {
                for (const auto &block: chunk.blocks | std::ranges::views::reverse) {
                    ++total_blocks;
                    if (block.era >= 2) [[likely]] {
                        if (const auto pool_it = stake_dist.find(block.pool_id); pool_it != stake_dist.end()) [[likely]] {
                            if (const auto [it, added] = seen_pools.emplace(block.pool_id); added) {
                                signing_stake += static_cast<double>(pool_it->second.rel_stake);
                            } else {
                                ++repeated_signer_blocks;
                            }
                        } else if (!genesis_pools.contains(block.pool_id)) {
                            ++obsolete_signer_blocks;
                        }
                        tail_stake.try_emplace(block.point(), signing_stake);
                        if (signing_stake > 0.5) {
                            log_stats();
                            return tail_stake;
                        }
                    }
                }
            }
            log_stats();
            return tail_stake;
        }

        cardano::optional_point core_tip() const
        {
            timer t { "core_tip estimation", logger::level::debug };
            for (const auto &[point, rel_stake]: tail_relative_stake()) {
                if (rel_stake > 0.5)
                    return point;
            }
            return {};
        }

        void my_on_block_validate(const cardano::block_base &blk) const
        {
            auto slot = blk.slot();
            if (!blk.signature_ok()) [[unlikely]]
                throw error(fmt::format("validation of the block signature at slot {} failed!", slot));
            if (blk.era() > 0 && !blk.body_hash_ok()) [[unlikely]]
                throw error(fmt::format("validation of the block body hash at slot {} failed!", slot));
            switch (blk.era()) {
                case 0: {
                    static auto boundary_issuer_vkey = cardano::vkey::from_hex("0000000000000000000000000000000000000000000000000000000000000000");
                    if (blk.issuer_vkey() != boundary_issuer_vkey)
                        throw error(fmt::format("boundary block contains an unexpected issuer_vkey: {}", blk.issuer_vkey()));
                    break;
                }
                case 1: {
                    if (!_cr.config().byron_issuers.contains(blk.issuer_vkey())) [[unlikely]]
                        throw error(fmt::format("unexpected Byron issuer_vkey: {}", blk.issuer_vkey()));
                    break;
                }
                case 2:
                case 3:
                case 4:
                case 5:
                case 6:
                case 7:
                    // do nothing here, the block signer's eligibility is tested later process_vrf_chunks
                    break;
                default:
                    throw error(fmt::format("unsupported block era: {}", blk.era()));
            }
        }

        void my_on_chunk_add(const storage::chunk_info &chunk, const bool fast=false)
        {
            subchain sc {
                chunk.offset, chunk.data_size, chunk.blocks.size(), 0,
                chunk.first_slot, chunk.first_block_hash(), chunk.last_slot, chunk.last_block_hash
            };
            if (!fast) {
                for (const auto &blk: chunk.blocks) {
                    if (blk.era < 2)
                        ++sc.valid_blocks;
                }
            } else {
                sc.valid_blocks = sc.num_blocks;
            }
            _parse_register_subchain(std::move(sc));
        }

        cardano::optional_point tip() const
        {
            if (const auto last_block = _cr.last_valid_block(); last_block)
                return cardano::point { last_block->hash, last_block->slot, last_block->height };
            return {};
        }

        cardano::optional_slot can_export(const cardano::optional_point &immutable_tip) const
        {
            if (const auto snap = _best_exportable_snapshot(immutable_tip); snap)
                return snap->last_slot;
            return {};
        }

        std::string node_export(const std::filesystem::path &ledger_dir, const cardano::optional_point &immutable_tip, const int prio_base) const
        {
            // export only the reserve (penultimate snapshot) so that Cardano Node have some space to rollback blocks if necessary
            if (const auto best_snap = _best_exportable_snapshot(immutable_tip); best_snap && best_snap->end_offset) {
                logger::info("selected the ledger snapshot with end_offset {} last_slot {} for export",
                    best_snap->end_offset, cardano::slot { best_snap->last_slot, _cr.config() });
                const auto path = (ledger_dir / fmt::format("{}_dt", best_snap->last_slot)).string();
                if (best_snap->end_offset == _state.end_offset() && best_snap->last_slot == _state.last_slot()) {
                    const auto snap_tip = _cr.find_block_by_offset(best_snap->end_offset - 1).point();
                    _state.save_node(path, snap_tip, prio_base);
                } else {
                    cardano::ledger::state snap_state { _cr.config(), _cr.sched() };
                    snap_state.load_zpp(_storage_path("ledger", best_snap->end_offset));
                    const auto snap_tip = _cr.find_block_by_offset(best_snap->end_offset - 1).point();
                    snap_state.save_node(path, snap_tip, prio_base);
                }
                return path;
            }
            throw error("do not have an exportable snapshot");
        }

        const cardano::ledger::state &state() const
        {
            return _state;
        }

        const snapshot_set &snapshots() const
        {
            return _snapshots;
        }

        void load_snapshot(cardano::ledger::state &st, const snapshot &snap) const
        {
            st.load_zpp(_storage_path("ledger", snap.end_offset));
            if (st.end_offset() != snap.end_offset)
                throw error(fmt::format("loaded state does not match the recorded end offset: {} != {}", st.end_offset(), snap.end_offset));
            if (st.end_offset() != st.valid_end_offset())
                throw error(fmt::format("validator state is in inconsistent state valid_end_offset: {} vs end_offset: {}", st.valid_end_offset(), st.end_offset()));
        }
    private:
        static constexpr uint64_t snapshot_hifreq_end_offset_range = static_cast<uint64_t>(1) << 30;
        static constexpr uint64_t snapshot_hifreq_distance = static_cast<uint64_t>(1) << 27;
        static constexpr uint64_t snapshot_normal_distance = indexer::merger::part_size * 2;

        using timed_update_list = vector<index::timed_update::item>;
        using epoch_task_map = map<uint64_t, cardano::slot_range>;

        chunk_registry &_cr;
        const std::filesystem::path _validate_dir;
        const std::string _state_path;
        const std::string _state_pre_path;
        cardano::ledger::state _state;
        std::atomic_bool _validation_running { false };
        mutable mutex::unique_lock::mutex_type _next_task_mutex alignas(mutex::alignment) {};
        uint64_t _next_end_offset = 0;
        epoch_task_map _next_tasks {};
        snapshot_set _snapshots {};
        std::optional<snapshot> _reserve_snapshot {};
        chunk_processor _proc {
            [this] { return my_end_offset(); },
            [this] { my_start_tx(); },
            [this] { my_prepare_tx(); },
            [this] { my_rollback_tx(); },
            [this] { my_commit_tx(); },
            [this](const auto &new_tip, const auto track) { my_truncate(new_tip, track); },
            [this](const auto &block) { my_on_block_validate(block); },
            [this](const auto &chunk) { my_on_chunk_add(chunk); },
            [this](const auto epoch, const auto &info) { my_on_epoch_update(epoch, info); },
        };

        const snapshot *_best_exportable_snapshot(const cardano::optional_point &imm_tip) const
        {
            if (!_snapshots.empty() && imm_tip)
                return _snapshots.best([&](const auto &snap) { return snap.exportable && snap.end_offset <= imm_tip->end_offset; });
            return nullptr;
        }

        void _load_state()
        {
            uint64_t end_offset = 0;
            _state.clear();
            _snapshots.clear();
            chunk_registry::file_set known_files {};
            if (std::filesystem::exists(_state_path)) {
                known_files.emplace(_state_path);
                const auto j_snapshots = json::load(_state_path).as_array();
                for (const auto &j_s: j_snapshots) {
                    const auto snap = snapshot::from_json(j_s.as_object());
                    if (const auto snap_path = _storage_path("ledger", snap.end_offset); std::filesystem::exists(snap_path)) {
                        _snapshots.emplace(std::move(snap));
                        known_files.insert(snap_path);
                    }
                }
                if (!_snapshots.empty())
                    end_offset = _load_state_snapshot(*_snapshots.rbegin());
            }
            for (auto &e: std::filesystem::directory_iterator(_validate_dir)) {
                const auto canon_path = std::filesystem::weakly_canonical(e.path()).string();
                if (e.is_regular_file() && !known_files.contains(canon_path))
                    _cr.remover().mark(canon_path);
            }
            logger::info("validator snapshot has data up to offset: {}", end_offset);
        }

        void _save_json_snapshots(const std::string &path)
        {
            json::array j_snapshots {};
            for (const auto &j_snap: _snapshots)
                j_snapshots.emplace_back(j_snap.to_json());
            json::save_pretty(path, j_snapshots);
        }

        uint64_t _load_state_snapshot(const snapshot &snap)
        {
            load_snapshot(_state, snap);
            return _state.end_offset();
        }

        std::string _storage_path(const std::string_view &prefix, uint64_t end_offset) const
        {
            return std::filesystem::weakly_canonical(_validate_dir / fmt::format("{}-{:013}.bin", prefix, end_offset)).string();
        }

        void _save_reserve_snapshot()
        {
            if (_state.end_offset() && _cr.num_chunks()) {
                const auto &first_block = _cr.chunks().begin()->second.blocks.front();
                const auto &last_block = _cr.find_block_by_offset(_state.end_offset() - 1);
                // the sub-chains will be checked at in prepare_tx
                // here creating the sub-chains as they must be when the snapshot is valid
                subchain_list tmp_sc {};
                tmp_sc.add(subchain { 0, _state.end_offset(), last_block.height, last_block.height,
                    first_block.slot, first_block.hash, last_block.slot, last_block.hash });
                _state.save_zpp(_storage_path("ledger-reserve", _state.end_offset()), std::make_unique<subchain_list>(std::move(tmp_sc)));
                _reserve_snapshot.emplace(_state);
            }
        }

        void _save_state_snapshot()
        {
            logger::debug("initiating the saving of the validator state snapshot epoch: {} end_offset: {}", _state.epoch(), _state.end_offset());
            timer t {
                fmt::format("saved the ledger's state snapshot epoch: {} end_offset: {}", _state.epoch(), _state.end_offset()),
                    logger::level::info };
            logger::debug("saving VRF state");
            logger::debug("saving the validator state");
            _state.save_zpp(_storage_path("ledger", _state.end_offset()));
            logger::debug("recording the new snapshot");
            snapshot latest { _state };
            _snapshots.emplace(std::move(latest));
        }

        void _remove_temporary_data()
        {
            timer t { "validator::remove_temporary_data" };
            const auto &indexer = _cr.indexer();
            for (auto &[name, idxr_ptr]: indexer.indexers()) {
                if (!idxr_ptr->mergeable()) {
                    std::filesystem::remove_all(idxr_ptr->chunk_dir());
                    idxr_ptr->reset();
                }
            }
            for (const auto &name: { "epoch-delta", "outflow" }) {
                std::filesystem::remove_all(indexer.idx_dir() / name);
                if (indexer.indexers().contains(name))
                    indexer.indexers().at(name)->reset();
            }
        }

        // Compressed data is there and most indices have already been created and merged
        // Recreate only temporary indices and apply updates without revalidating the data
        // since it already has been validated up to this point
        void _apply_ledger_updates_fast()
        {
            std::vector<index::indexer_base *> idxrs {};
            for (const auto &[name, idxr]: _cr.indexer().indexers()) {
                if (!idxr->mergeable())
                    idxrs.emplace_back(idxr.get());
            }
            _next_end_offset = _cr.num_bytes();
            _next_tasks.clear();
            const auto state_start_offset = _state.end_offset();
            if (state_start_offset < _cr.num_bytes()) {
                auto it = _cr.find_offset_it(state_start_offset);
                if (it->second.offset != state_start_offset)
                    throw error("internal error: a chunk that doesn't begin right after the snapshot's end");
                for (; it != _cr.chunks().end(); ++it) {
                    const auto &chunk = it->second;
                    const auto chunk_path = _cr.full_path(it->second.rel_path());
                    for (const auto slot: { chunk.first_slot, chunk.last_slot }) {
                        const auto chunk_slot = _cr.make_slot(slot);
                        const auto [task_it, task_created] = _next_tasks.try_emplace(chunk_slot.epoch(), slot);
                        if (!task_created)
                            task_it->second.update(slot);
                    }
                    _cr.sched().submit_void("parse-fast", 100, [this, state_start_offset, chunk, chunk_path, &idxrs] {
                        indexer::chunk_indexer_list chunk_indexers {};
                        for (auto *idxr_ptr: idxrs)
                            chunk_indexers.emplace_back(idxr_ptr->make_chunk_indexer("update", chunk.offset));
                        const auto raw_data = zstd::read(chunk_path);
                        cbor::zero2::decoder dec { raw_data };
                        while (!dec.done()) {
                            auto &block_tuple = dec.read();
                            const cardano::block_container blk { numeric_cast<uint64_t>(chunk.offset + block_tuple.data_begin() - raw_data.data()), block_tuple, _cr.config() };
                            if (blk.offset() >= state_start_offset) {
                                for (auto &idxr: chunk_indexers)
                                    idxr->index(blk);
                                blk->foreach_tx([&](const auto &tx) {
                                    for (auto &idxr: chunk_indexers)
                                        idxr->index_tx(tx);
                                });
                                blk->foreach_invalid_tx([&](const auto &tx) {
                                    for (auto &idxr: chunk_indexers)
                                        idxr->index_invalid_tx(tx);
                                });
                            }
                        }
                        my_on_chunk_add(chunk, true);
                    });
                }
                _cr.sched().process(true);
            }
            mutex::unique_lock lk { _next_task_mutex };
            _schedule_validation(std::move(lk), true);
            _cr.sched().process(true);
        }

        void _schedule_validation(mutex::unique_lock &&next_task_lk, bool fast)
        {
            // move, so that it unlocks on stack unrolling
            mutex::unique_lock lk { std::move(next_task_lk) };
            bool exp_false = false;
            if (_validation_running.compare_exchange_strong(exp_false, true)) {
                _cr.sched().submit_void("validate", 400, [this, fast] {
                    try {
                        mutex::unique_lock lk2 { _next_task_mutex };
                        while (_state.end_offset() < _next_end_offset && !_next_tasks.empty()) {
                            logger::debug("acquired _next_task mutex and configuring the validation task");
                            const auto start_offset = _state.end_offset();
                            const auto end_offset = _next_end_offset;
                            const auto tasks = _next_tasks;
                            _next_tasks.clear();
                            const auto ready_slices = _cr.indexer().slices(end_offset);
                            lk2.unlock();
                            logger::debug("merging subchains from the same epoch");
                            _state.merge_same_epoch_subchains();
                            logger::debug("begin applying ledger state updates");
                            _apply_ledger_state_updates(tasks, ready_slices, fast);
                            if (_state.end_offset() == start_offset)
                                throw error("the application of state has failed to make any progress");
                            logger::debug("done applying ledger state updates, acquiring _next_task lock");
                            lk2.lock();
                        }
                        _validation_running = false;
                    } catch (const std::exception &ex) {
                        logger::error("validation has failed: {}", ex.what());
                        _validation_running = false;
                        throw;
                    }
                });
            }
        }

        void _parse_register_subchain(subchain &&sc)
        {
            if (sc.num_blocks) [[likely]] {
                if (const auto valid_point = _state.add_subchain(std::move(sc)); valid_point)
                    _cr.report_progress("validate", { valid_point->slot, valid_point->end_offset });
            } else {
                throw error(fmt::format("chunk at offset {} contains no blocks!", sc.offset));
            }
        }

        template<typename I, typename T>
        std::optional<uint64_t> _gather_updates(vector<T> &updates, const std::string &name, const cardano::slot_range &slots, const uint64_t min_offset)
        {
            const auto &updated_chunks = dynamic_cast<I &>(*_cr.indexer().indexers().at(name)).chunks(slots);
            updates.clear();
            std::optional<uint64_t> min_chunk_id {};
            if (!updated_chunks.empty()) {
                for (const uint64_t chunk_id: updated_chunks) {
                    if (chunk_id >= min_offset) {
                        if (!min_chunk_id || *min_chunk_id > chunk_id)
                            min_chunk_id = chunk_id;
                        const auto chunk_path = fmt::format("{}.bin", _cr.indexer().indexers().at(name)->chunk_path("update", chunk_id));
                        vector<T> chunk_updates {};
                        zpp::load_zstd(chunk_updates, chunk_path);
                        for (const auto &u: chunk_updates)
                            updates.emplace_back(std::move(u));
                    }
                }
                std::sort(updates.begin(), updates.end());
            }
            return min_chunk_id;
        }

        void _load_utxo_updates(updates_t &updates, const uint64_t epoch, const uint64_t min_offset, const std::string &name, const index::chunk_list &updated_chunks)
        {
            index::chunk_list relevant_chunks {};
            for (const uint64_t c_id: updated_chunks) {
                if (c_id >= min_offset)
                    relevant_chunks.emplace_back(c_id);
            }
            if (!relevant_chunks.empty()) {
                const std::string task_group = fmt::format("ledger-state:load-utxo-updates:epoch-{}", epoch);
                updates.utxos.resize(relevant_chunks.size());
                _cr.sched().wait_all_done(task_group, relevant_chunks.size(), [&] {
                    for (size_t ci = 0; ci < relevant_chunks.size(); ++ci) {
                        const auto chunk_path = fmt::format("{}.bin", _cr.indexer().indexers().at(name)->chunk_path("update", relevant_chunks[ci]));
                        _cr.sched().submit_void(task_group, 1000, [ci, chunk_path, &updates] {
                            zpp::load_zstd(updates.utxos[ci], chunk_path);
                            std::filesystem::remove(chunk_path);
                        });
                    }
                });
            }
        }

        void _apply_ledger_state_updates_for_epoch(uint64_t e, const cardano::slot_range &slots, bool fast)
        {
            timer te { fmt::format("apply_ledger_state_updates for epoch {}", e) };
            try {
                const auto last_epoch = _state.epoch();
                const auto last_offset = _state.end_offset();
                if (!last_offset || last_epoch < e)
                    _state.start_epoch(e);

                std::optional<uint64_t> min_epoch_offset;
                {
                    updates_t updates {};
                    min_epoch_offset = _gather_updates<index::block_fees::indexer>(updates.blocks, "block-fees", slots, last_offset);
                    if (!min_epoch_offset)
                        return;
                    _gather_updates<index::timed_update::indexer>(updates.timed, "timed-update", slots, last_offset);
                    _load_utxo_updates(updates, e, last_offset, "utxo", dynamic_cast<index::utxo::indexer &>(*_cr.indexer().indexers().at("utxo")).chunks(slots));
                    _state.process_updates(std::move(updates));
                }

                const auto vrf_chunks = dynamic_cast<index::vrf::indexer &>(*_cr.indexer().indexers().at("vrf")).chunks(slots);
                if (!vrf_chunks.empty())
                    _process_vrf_update_chunks(*min_epoch_offset, vrf_chunks, fast);

                if (_cr.tx()->target && _state.params().protocol_ver.major >= 3) {
                    if (const auto target_slot = _cr.make_slot(_cr.tx()->target->slot); target_slot.epoch() >= 2) {
                        const auto target_epoch_start = cardano::slot::from_epoch(target_slot.epoch(), _cr.config());
                        const bool prev_epoch_ok = (_cr.tx()->target->slot - target_epoch_start) >= _cr.config().shelley_randomness_stabilization_window;
                        if (prev_epoch_ok) {
                            if (_state.epoch() == target_slot.epoch() - 1)
                                _save_reserve_snapshot();
                        } else {
                            if (_state.epoch() == target_slot.epoch() - 2)
                                _save_reserve_snapshot();
                        }
                    }
                }
            } catch (const std::exception &ex) {
                logger::error("apply_updates for epoch: {} std::exception: {}", e, ex.what());
                throw error(fmt::format("failed to process epoch {} updates", e), ex);
            } catch (...) {
                logger::error("apply_updates for epoch: {} unknown exception", e);
                throw error(fmt::format("failed to process epoch {} updates cased by an unknown exception", e));
            }
        }

        void _apply_ledger_state_updates(const epoch_task_map &tasks, const indexer::slice_list &/*slices*/, const bool fast)
        {
            const auto first_epoch = tasks.begin()->first;
            const auto last_epoch = tasks.rbegin()->first;
            timer t { fmt::format("validator::_apply_ledger_state_updates first_epoch: {} last_epoch: {} fast: {}", first_epoch, last_epoch, fast), logger::level::debug };
            // add extra snapshots closer to the tip since rollbacks are more likely there
            for (const auto &[e, slots]: tasks) {
                timer te { fmt::format("apply ledger updates for epoch: {}", e) };
                try {
                    _apply_ledger_state_updates_for_epoch(e, slots, fast);
                    logger::info("applied ledger updates for epoch: {} end offset: {} utxos: {}", _state.epoch(), _state.end_offset(), _state.utxos().size());
                } catch (const std::exception &ex) {
                    logger::error("failed to process epoch {} updates: {}", e, ex.what());
                    throw error(fmt::format("failed to process epoch {} updates: {}", e, ex.what()));
                } catch (...) {
                    logger::error("failed to process epoch {} updates: unknown exception", e);
                    throw;
                }
            }
        }

        void _validate_epoch_leaders(const uint64_t epoch, const uint64_t epoch_min_offset, const std::shared_ptr<vector<index::vrf::item>> &vrf_updates_ptr,
            const std::shared_ptr<operating_pool_map> &pool_dist_ptr,
            const cardano::vrf_nonce &nonce_epoch, const cardano::vrf_nonce &uc_nonce, const cardano::vrf_nonce &uc_leader,
            const size_t start_idx, const size_t end_idx)
        {
            timer t { fmt::format("validate_leaders for epoch {} block indices from {} to {}", epoch, start_idx, end_idx), logger::level::trace };
            for (size_t vi = start_idx; vi < end_idx; ++vi) {
                const auto &item = vrf_updates_ptr->at(vi);
                if (item.era < 6) {
                    const auto leader_input = vrf_make_seed(uc_leader, item.slot, nonce_epoch);
                    if (!vrf03_verify(item.leader_result, item.vkey, item.leader_proof, leader_input))
                        throw error(fmt::format("leader VRF verification failed: epoch: {} slot {} era {}", epoch, item.slot, item.era));
                    auto nonce_input = vrf_make_seed(uc_nonce, item.slot, nonce_epoch);
                    if (!vrf03_verify(item.nonce_result, item.vkey, item.nonce_proof, nonce_input))
                        throw error(fmt::format("nonce VRF verification failed: epoch: {} slot {} era {}", epoch, item.slot, item.era));
                } else {
                    const auto vrf_input = vrf_make_input(item.slot, nonce_epoch);
                    if (!vrf03_verify(item.leader_result, item.vkey, item.leader_proof, vrf_input))
                        throw error(fmt::format("VRF verification failed: epoch: {} slot {} era {}", epoch, item.slot, item.era));
                }
                if (!_state.pbft_pools().contains(item.pool_id)) {
                    const auto pool_it = pool_dist_ptr->find(item.pool_id);
                    if (pool_it == pool_dist_ptr->end())
                        throw error(fmt::format("epoch {} pool-stake distribution misses block-issuing pool id {}!", epoch, item.pool_id));
                    const auto &rel_stake = pool_it->second.rel_stake;
                    if (item.era < 6) {
                        if (!vrf_leader_is_eligible(item.leader_result, 0.05, rel_stake))
                            throw error(fmt::format("Leader-eligibility check failed for block at slot {} issued by {}: leader_result: {} rel_stake: {}",
                                item.slot, item.pool_id, item.leader_result, rel_stake));
                    } else {
                        if (!vrf_leader_is_eligible(vrf_leader_value(item.leader_result), 0.05, rel_stake))
                            throw error(fmt::format("era 6 Leader-eligibility check failed for block at slot {} issued by {}: leader_result: {} rel_stake: {}",
                                item.slot, item.pool_id, item.leader_result, rel_stake));
                    }
                }
            }
            if (const auto new_valid_tip = _state.mark_subchain_valid(epoch_min_offset, end_idx - start_idx); new_valid_tip)
                _cr.report_progress("validate", { new_valid_tip->slot, new_valid_tip->end_offset });
        }

        void _process_vrf_update_chunks(uint64_t epoch_min_offset, const vector<uint64_t> &chunks, const bool fast)
        {
            const auto epoch = _state.epoch();
            timer t { fmt::format("processed VRF nonce updates for epoch {}", epoch) };
            const auto vrf_updates_ptr = std::make_shared<vector<index::vrf::item>>();
            for (const uint64_t chunk_id: chunks) {
                const auto chunk_path = fmt::format("{}.bin", _cr.indexer().indexers().at("vrf")->chunk_path("update", chunk_id));
                vector<index::vrf::item> chunk_updates {};
                zpp::load_zstd(chunk_updates, chunk_path);
                vrf_updates_ptr->reserve(vrf_updates_ptr->size() + chunk_updates.size());
                for (const auto &u: chunk_updates)
                    vrf_updates_ptr->emplace_back(u);
            }
            if (!vrf_updates_ptr->empty()) {
                std::sort(vrf_updates_ptr->begin(), vrf_updates_ptr->end());
                if (!fast) {
                    const auto pool_dist_ptr = std::make_shared<operating_pool_map>(_state.pool_stake_dist());
                    const auto &nonce_epoch = _state.vrf_state().nonce_epoch();
                    const auto &uc_nonce = _state.vrf_state().uc_nonce();
                    const auto &uc_leader = _state.vrf_state().uc_leader();
                    static constexpr size_t batch_size = 250;
                    static std::string task_name { validate_leaders_task };
                    for (size_t start = 0; start < vrf_updates_ptr->size(); start += batch_size) {
                        auto end = std::min(start + batch_size, vrf_updates_ptr->size());
                        _cr.sched().submit_void(task_name, -static_cast<int64_t>(epoch), [this, epoch, epoch_min_offset, vrf_updates_ptr, pool_dist_ptr, nonce_epoch, uc_nonce, uc_leader, start, end] {
                            _validate_epoch_leaders(epoch, epoch_min_offset, vrf_updates_ptr, pool_dist_ptr, nonce_epoch, uc_nonce, uc_leader, start, end);
                        }, chunk_offset_t { epoch_min_offset });
                    }
                }
                _state.vrf_process_updates(*vrf_updates_ptr);
            }
        }
    };

    incremental::incremental(chunk_registry &cr): _impl { std::make_unique<impl>(cr) }
    {
    }

    incremental::~incremental() =default;

    cardano::amount incremental::unspent_reward(const cardano::stake_ident &id) const
    {
        return _impl->unspent_reward(id);
    }

    cardano::tail_relative_stake_map incremental::tail_relative_stake() const
    {
        return _impl->tail_relative_stake();
    }

    cardano::optional_slot incremental::can_export(const cardano::optional_point &immutable_tip) const
    {
        return _impl->can_export(immutable_tip);
    }

    std::string incremental::node_export(const std::filesystem::path &ledger_dir, const cardano::optional_point &immutable_tip, const int prio_base) const
    {
        return _impl->node_export(ledger_dir, immutable_tip, prio_base);
    }

    cardano::optional_point incremental::core_tip() const
    {
        return _impl->core_tip();
    }

    cardano::optional_point incremental::tip() const
    {
        return _impl->tip();
    }

    const cardano::ledger::state &incremental::state() const
    {
        return _impl->state();
    }

    const snapshot_set &incremental::snapshots() const
    {
        return _impl->snapshots();
    }

    void incremental::load_snapshot(cardano::ledger::state &st, const snapshot &snap) const
    {
        return _impl->load_snapshot(st, snap);
    }
}