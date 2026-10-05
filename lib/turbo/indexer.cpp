/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <array>
#include <turbo/common/scheduler.hpp>
#include <turbo/cardano.hpp>
#include <turbo/chunk-registry.hpp>
#include <turbo/index/pay-ref.hpp>
#include <turbo/index/stake-ref.hpp>
#include <turbo/index/tx.hpp>
#include <turbo/index/txo-use.hpp>
#include <turbo/indexer.hpp>

namespace turbo::indexer {
    namespace {
        struct index_state_t {
            slice_list slices;
            std::set<std::string> unused_files;
            bool repaired = false;
        };

        index_state_t load_state(const std::filesystem::path &dir, const std::set<std::string> &names,
            const storage::chunk_map &chunks, const bool strict, const file_remover::remove_point_map &marked={})
        {
            index_state_t state;
            for (const auto &name: names) {
                const auto path = dir / name;
                if (!std::filesystem::is_directory(path)) [[unlikely]] {
                    throw error(fmt::format("missing index directory {}: open the registry in validation mode first", path.string()));
                }
                for (const auto &entry: std::filesystem::directory_iterator(path)) {
                    if (entry.is_regular_file()) {
                        state.unused_files.emplace(fmt::format("{}/{}/{}", dir.string(), name, entry.path().filename().string()));
                    }
                }
            }
            const auto path = dir / "state.json";
            if (std::filesystem::exists(path)) {
                uint64_t end_offset = 0;
                json::array metadata;
                try {
                    metadata = json::load(path.string()).as_array();
                } catch (const std::exception &ex) {
                    if (strict)
                        throw;
                    logger::warn("cannot load index metadata; rebuilding from stored blocks: {}", ex.what());
                    state.repaired = true;
                }
                for (const auto &value: metadata) {
                    merger::slice slice;
                    try {
                        slice = merger::slice::from_json(value.as_object());
                    } catch (const std::exception &ex) {
                        if (strict)
                            throw;
                        logger::warn("ignoring invalid index metadata and its successors: {}", ex.what());
                        state.repaired = true;
                        break;
                    }
                    std::vector<std::string> files;
                    for (const auto &name: names) {
                        files.emplace_back(index::indexer_base::reader_path(dir.string(), name, slice.slice_id));
                    }
                    const bool complete = std::ranges::all_of(files, [&](const auto &file) {
                        return state.unused_files.contains(file) && !index::indexer_base::temporary_file(file);
                    });
                    if (!complete || slice.offset != end_offset || !slice.size || slice.end_offset() < slice.offset
                            || !storage::matches_block_boundary(chunks, slice.end_offset(), slice.max_slot)) [[unlikely]] {
                        if (strict) {
                            throw error(fmt::format("inconsistent index slice {}: open the registry in validation mode first", slice.slice_id));
                        }
                        logger::warn("ignoring inconsistent index slice {} and its successors", slice.slice_id);
                        state.repaired = true;
                        break;
                    }
                    for (const auto &file: files) {
                        if (strict && marked.contains(file)) [[unlikely]] {
                            throw error(fmt::format("index file {} is marked for deletion: open the registry in validation mode first", file));
                        }
                        state.unused_files.erase(file);
                    }
                    end_offset = slice.end_offset();
                    state.slices.emplace_back(std::move(slice));
                }
            }
            if (strict && !state.unused_files.empty()) [[unlikely]] {
                throw error(fmt::format("unused index file {}: open the registry in validation mode first", *state.unused_files.begin()));
            }
            return state;
        }
    }

    slice_list inspect_state(const std::filesystem::path &data_dir, const storage::chunk_map &chunks,
        const file_remover::remove_point_map &marked, const bool required)
    {
        const auto dir = std::filesystem::weakly_canonical(data_dir / "index");
        const bool state_exists = std::filesystem::exists(dir / "state.json");
        if (!state_exists && (required || std::filesystem::exists(dir))) [[unlikely]] {
            throw error("missing index state: open the registry in validation mode first");
        }
        if (!state_exists) {
            return {};
        }
        if (std::filesystem::exists(dir / "state-pre.json")) [[unlikely]] {
            throw error("unfinished index transaction: open the registry in validation mode first");
        }
        const auto require_empty = [&](const char *name) {
            const auto path = dir / name;
            if (std::filesystem::exists(path) && (!std::filesystem::is_directory(path) || !std::filesystem::is_empty(path))) [[unlikely]] {
                throw error(fmt::format("unfinished ledger index data {}: open the registry in validation mode first", path.string()));
            }
        };
        for (const auto *name: index_layout_t::ledger) {
            require_empty(name);
        }
        for (const auto *name: index_layout_t::auxiliary) {
            require_empty(name);
        }
        return load_state(dir, { index_layout_t::query.begin(), index_layout_t::query.end() }, chunks, true, marked).slices;
    }

    void clean_up_ledger_data(const indexer_map &indexers, const std::filesystem::path &index_dir)
    {
        for (const auto &[name, indexer]: indexers) {
            if (!indexer->mergeable()) {
                std::filesystem::remove_all(indexer->chunk_dir());
                indexer->reset();
            }
        }
        for (const auto *name: index_layout_t::auxiliary) {
            std::filesystem::remove_all(index_dir / name);
        }
    }

    void chunk_indexer_list_t::index_block(const cardano::block_container &block) const
    {
        for (const auto &idx: *this) {
            idx->index(block);
        }
        block->foreach_tx([&](const auto &tx) {
            for (const auto &idx: *this) {
                idx->index_tx(tx);
            }
        });
        block->foreach_invalid_tx([&](const auto &tx) {
            for (const auto &idx: *this) {
                idx->index_invalid_tx(tx);
            }
        });
    }

    struct incremental::impl {
        impl(chunk_registry &cr, indexer_map &&indexers, const storage::chunk_map &chunks, std::optional<slice_list> slices)
            : _cr { cr }, _indexers { std::move(indexers) }, _idx_dir { storage_dir(_cr.data_dir().string()) },
                _index_state_path { (_idx_dir / "state.json").string() },
                _mergeable { _mergeable_indexers(_indexers) }
        {
            if (!slices && cr.operating_mode() != chunk_registry::mode::validate) [[unlikely]] {
                throw error("index maintenance requires opening the registry in validation mode");
            }
            file::set_max_open_files();
            bool repaired = false;
            if (!slices) {
                clean_up_ledger_data(_indexers, _idx_dir);
                auto state = load_state(_idx_dir, _mergeable, chunks, false);
                for (const auto &path: state.unused_files) {
                    _cr.remover().mark(path);
                }
                repaired = state.repaired;
                slices = std::move(state.slices);
            }
            const auto end_offset = slices->empty() ? 0 : slices->back().end_offset();
            for (const auto &slice: *slices) {
                _retain_slice(slice);
            }
            if (end_offset != indexed_bytes()) [[unlikely]] {
                throw error(fmt::format("internal error: indexed size calculation is incorrect: {} vs {}", end_offset, indexed_bytes()));
            }
            if (cr.operating_mode() == chunk_registry::mode::validate && (repaired || !std::filesystem::exists(_index_state_path))) {
                _save_json_slices(_index_state_path);
            }
            _cr.register_processor(_proc);
            logger::info("indices have data up to offset {}", end_offset);
        }

        ~impl()
        {
            _cr.remove_processor(_proc);
        }

        slice_list slices(std::optional<uint64_t> end_offset={}) const
        {
            slice_list copy {};
            mutex::scoped_lock lk { _slices_mutex };
            copy.reserve(_slices.size());
            for (const auto &[offset, s]: _slices) {
                if (!end_offset || *end_offset >= s.end_offset())
                    copy.emplace_back(s);
            }
            return copy;
        }

        slice_path_list reader_paths(const std::string &name, const slice_list &slcs) const
        {
            slice_path_list paths;
            for (const auto &slice: slcs)
                paths.emplace_back(_indexers.at(name)->reader_path(slice.slice_id));
            return paths;
        }

        slice_path_list reader_paths(const std::string &name) const
        {
            return reader_paths(name, slices());
        }

        uint64_t indexed_bytes() const
        {
            mutex::scoped_lock lk { _slices_mutex };
            return _slices.continuous_size();
        }

        const indexer_map &indexers() const
        {
            return _indexers;
        }

        const std::filesystem::path &idx_dir() const
        {
            return _idx_dir;
        }

        chunk_indexer_list_t make_chunk_indexers(uint64_t chunk_offset)
        {
            chunk_indexer_list_t chunk_indexers {};
            for (auto &[name, idxr_ptr]: _indexers)
                chunk_indexers.emplace_back(idxr_ptr->make_chunk_indexer("update", chunk_offset));
            return chunk_indexers;
        }
    private:
        chunk_registry &_cr;
        const indexer_map _indexers;
        const std::filesystem::path _idx_dir;
        const std::string _index_state_path;

        const std::set<std::string> _mergeable;
        mutable mutex::unique_lock::mutex_type _slices_mutex alignas(mutex::alignment) {};
        merger::tree _slices {};
        uint64_t _merge_next_offset = 0;
        mutable mutex::unique_lock::mutex_type _epoch_slices_mutex alignas(mutex::alignment) {};
        std::map<uint64_t, merger::slice> _epoch_slices {};
        // rollback tracking
        std::vector<merger::slice> _slices_truncated {};
        std::vector<merger::slice> _slices_added {};
        chunk_processor _proc {
            .end_offset=[this] { return _idx_end_offset(); },
            .start_tx=[this] { _idx_start_tx(); },
            .prepare_tx=[this] { _idx_prepare_tx(); },
            .rollback_tx=[this] { _idx_rollback_tx(); },
            .commit_tx=[this] { _idx_commit_tx(); },
            .truncate=[this](const auto &new_tip, const auto track) { _idx_truncate(new_tip, track); },
            .on_epoch_update=[this](const auto epoch, const auto &info) { _idx_on_epoch_update(epoch, info); },
            .stage_tx=[this] { _idx_stage_tx(); }
        };

        void _retain_slice(const merger::slice &slice)
        {
            _slices.add(slice);
            if (_cr.operating_mode() == chunk_registry::mode::validate) {
                for (const auto &name: _mergeable) {
                    _cr.remover().unmark(_indexers.at(name)->reader_path(slice.slice_id));
                }
            }
        }

        static std::set<std::string> _mergeable_indexers(const indexer_map &indexers)
        {
            std::set<std::string> m {};
            for (auto &[name, idxr_ptr]: indexers) {
                if (idxr_ptr && idxr_ptr->mergeable())
                    m.emplace(name);
            }
            return m;
        }

        void _merge_slice(const merger::slice &output_slice, const std::vector<std::string> &input_slices,
            const int64_t prio_base, const std::function<void()> &on_merge)
        {
            const auto indices_awaited = std::make_shared<std::atomic_size_t>(_mergeable.size());
            for (const auto &idxr_name: _mergeable) {
                auto idxr_ptr = _indexers.at(idxr_name).get();
                std::vector<std::string> input_paths {};
                for (const auto &slice_id: input_slices) {
                    if (idxr_ptr->disk_size(slice_id) > 0)
                        input_paths.emplace_back(idxr_ptr->reader_path(slice_id));
                }
                const auto output_path = idxr_ptr->write_path(output_slice.slice_id);
                const auto priority = prio_base - static_cast<int64_t>(output_slice.offset);
                _cr.sched().submit("merge:schedule-" + output_path, priority, [this, idxr_ptr, output_path, priority, input_paths, indices_awaited, on_merge] {
                    // merge tasks must have a higher priority + 50 so that the actual merge tasks free up file handles
                    // and other tied resources quicker than they are consumed
                    idxr_ptr->merge("merge:" + output_path, priority + 50, input_paths, output_path, [this, indices_awaited, output_path, priority, on_merge] {
                        if (--(*indices_awaited) == 0) {
                            _cr.sched().submit("merge:ready-" + output_path, priority, on_merge);
                        }
                    });
                });
            }
        }

        void _schedule_final_merge(mutex::unique_lock &epoch_slices_lk, const bool force=false)
        {
            if (!epoch_slices_lk) [[unlikely]]
                throw error("_cr.schedule_final_merge requires epoch_slices_mutex to be locked!");
            while (!_epoch_slices.empty()) {
                uint64_t total_size = 0;
                for (const auto &[epoch, slice]: _epoch_slices) {
                    if (slice.offset == _merge_next_offset + total_size) {
                        total_size += slice.size;
                    } else {
                        break;
                    }
                }
                if (total_size == 0)
                    break;
                if (total_size < merger::part_size && !force)
                    break;
                std::vector<std::string> input_slices {};
                uint64_t max_slot = 0;
                while (!_epoch_slices.empty() && _epoch_slices.begin()->second.offset < _merge_next_offset + total_size) {
                    const auto slice_it = _epoch_slices.begin();
                    if (slice_it->second.max_slot > max_slot)
                        max_slot = slice_it->second.max_slot;
                    input_slices.emplace_back(slice_it->second.slice_id);
                    _epoch_slices.erase(slice_it);
                }
                merger::slice output_slice { _merge_next_offset, total_size, max_slot };
                _merge_next_offset += total_size;
                epoch_slices_lk.unlock();
                _merge_slice(output_slice, input_slices, -1'000'000LL, [this, output_slice] {
                    // ensures notifications are sent only in their continuous order
                    std::vector<merger::slice> notify_slices {};
                    uint64_t merged_max_slot, merged_end_offset;
                    {
                        mutex::scoped_lock lk { _slices_mutex };
                        const auto old_indexed_size = _slices.continuous_size();
                        _retain_slice(output_slice);
                        _slices_added.emplace_back(output_slice);
                        const auto new_indexed_size = _slices.continuous_size();
                        if (new_indexed_size > old_indexed_size) {
                            for (auto slice_it = _slices.find(old_indexed_size); slice_it != _slices.end() && slice_it->second.end_offset() <= new_indexed_size; ++slice_it)
                                notify_slices.emplace_back(slice_it->second);
                        }
                        merged_max_slot = _slices.continuous_max_slot();
                        merged_end_offset = _slices.continuous_size();
                    }
                    _cr.report_progress("merge", { merged_max_slot, merged_end_offset });
                    for (const auto &ns: notify_slices) {
                        if (ns.size > 0)
                            logger::debug("new slice first epoch: {} last_epoch: {}", _cr.find_epoch(ns.offset), _cr.find_epoch(ns.end_offset() - 1));
                    }
                });
                epoch_slices_lk.lock();
            }
        }

        void _idx_truncate(const cardano::optional_point &new_tip, const bool track_changes)
        {
            if (const auto max_end_offset = new_tip ? new_tip->end_offset : 0; max_end_offset < _slices.continuous_size()) {
                timer t { fmt::format("truncate indices to max offset {}", max_end_offset), logger::level::info };
                std::vector<merger::slice> updated {};
                for (auto it = _slices.begin(); it != _slices.end(); ) {
                    const auto &s = it->second;
                    if (s.end_offset() <= max_end_offset) {
                        ++it;
                    } else { // s.end_offset() > max_end_offset
                        logger::trace("truncate index slice {}", s.slice_id);
                        if (track_changes)
                            _slices_truncated.emplace_back(s);
                        if (s.offset < max_end_offset) {
                            merger::slice new_slice { s.offset, std::min(s.size, max_end_offset - s.offset), new_tip->slot };
                            updated.emplace_back(new_slice);
                            for (auto &[name, idxr_ptr]: _indexers) {
                                _cr.sched().submit("truncate:init-" + name, 25, [&idxr_ptr, s, new_slice, max_end_offset] {
                                    idxr_ptr->schedule_truncate(s.slice_id, new_slice.slice_id, max_end_offset);
                                });
                            }
                        }
                        it = _slices.erase(it);
                    }
                }
                _cr.sched().process(true);
                for (auto &&new_slice: updated) {
                    _retain_slice(new_slice);
                    if (track_changes)
                        _slices_added.emplace_back(new_slice);
                }
            }
        }

        uint64_t _idx_end_offset() const
        {
            return _slices.continuous_size();
        }

        void _idx_start_tx()
        {
            _epoch_slices.clear();
            _merge_next_offset = _slices.empty() ? 0 : _slices.rbegin()->second.end_offset();
        }

        void _idx_prepare_tx()
        {
            timer t { "indexer::_prepare_tx" };
            // merge final not-yet merged epochs
            {
                mutex::unique_lock lk { _epoch_slices_mutex };
                _schedule_final_merge(lk, true);
            }
            _cr.sched().process(true);
        }

        void _idx_stage_tx()
        {
            for (const auto &[offset, slice]: _slices) {
                for (const auto &name: _mergeable) {
                    const auto filename = std::filesystem::path { _indexers.at(name)->write_path(slice.slice_id) }.filename();
                    _cr.stage_output(std::filesystem::path { "index" } / name / filename);
                    _cr.remover().unmark(index::indexer_base::reader_path(_idx_dir.string(), name, slice.slice_id));
                }
            }
            _save_json_slices(_cr.stage_path("index/state.json"));
            _cr.stage_output("index/state.json");
        }

        void _idx_rollback_tx()
        {
            // The registry discarded staged outputs before invoking rollback.
            for (const auto &s: _slices_added)
                _slices.erase(s.offset);
            _slices_added.clear();
            for (const auto &s: _slices_truncated) {
                _retain_slice(s);
            }
            _slices_truncated.clear();
        }

        void _idx_commit_tx()
        {
            for (const auto &s: _slices_truncated) {
                const auto retained = _slices.find(s.offset);
                if (retained == _slices.end() || retained->second.slice_id != s.slice_id) {
                    for (const auto &name: _mergeable) {
                        _cr.remover().mark(_indexers.at(name)->reader_path(s.slice_id));
                    }
                }
            }
            _slices_truncated.clear();
            _slices_added.clear();
        }

        void _idx_on_chunk_add(const storage::chunk_info &chunk, const cardano::parsed_block_list &blocks) const
        {
            chunk_indexer_list_t chunk_indexers {};
            for (auto &[name, idxr_ptr]: _indexers)
                chunk_indexers.emplace_back(idxr_ptr->make_chunk_indexer("update", chunk.offset));
            for (const auto &blk_ptr: blocks) {
                for (auto &idxr: chunk_indexers)
                    idxr->index(blk_ptr->blk);
            }
        }

        void _idx_on_epoch_update(const uint64_t epoch, const epoch_info &info)
        {
            std::vector<std::string> input_slices {};
            for (const auto &chunk_ptr: info.chunks()) {
                input_slices.emplace_back(fmt::format("update-{}", chunk_ptr->offset));
            }
            merger::slice output_slice { info.start_offset(), info.end_offset() - info.start_offset(), info.last_slot(), fmt::format("epoch-{}", epoch) };
            _merge_slice(output_slice, input_slices, -2'000'000LL, [this, epoch, output_slice] {
                mutex::unique_lock lk { _epoch_slices_mutex };
                _epoch_slices.emplace(epoch, output_slice);
                _schedule_final_merge(lk);
            });
        }

        void _save_json_slices(const std::string &path)
        {
            json::array j_slices {};
            for (const auto &[offset, slice]: _slices)
                j_slices.emplace_back(slice.to_json());
            json::save_pretty(path, j_slices);
        }
    };

    incremental::incremental(chunk_registry &cr, indexer_map &&indexers, const storage::chunk_map &chunks, std::optional<slice_list> slices)
        : _impl { std::make_unique<impl>(cr, std::move(indexers), chunks, std::move(slices)) }
    {
    }

    incremental::~incremental() =default;

    chunk_indexer_list_t incremental::make_chunk_indexers(const uint64_t chunk_offset)
    {
        return _impl->make_chunk_indexers(chunk_offset);
    }

    slice_list incremental::slices(const std::optional<uint64_t> end_offset) const
    {
        return _impl->slices(end_offset);
    }

    slice_path_list incremental::reader_paths(const std::string &name, const slice_list &slcs) const
    {
        return _impl->reader_paths(name, slcs);
    }

    slice_path_list incremental::reader_paths(const std::string &name) const
    {
        return reader_paths(name, slices());
    }

    const indexer_map &incremental::indexers() const
    {
        return _impl->indexers();
    }

    const std::filesystem::path &incremental::idx_dir() const
    {
        return _impl->idx_dir();
    }

    std::string incremental::storage_dir(const std::string &data_dir)
    {
        return chunk_registry::init_db_dir(data_dir + "/index").string();
    }

    slice_path_list multi_reader_paths(const std::string &idx_dir, const std::string &name, const slice_list &slices)
    {
        slice_path_list paths {};
        for (const auto &slice: slices)
        paths.emplace_back(index::indexer_base::reader_path(idx_dir, name, slice.slice_id));
        logger::trace("multi_reader_paths paths for index {}: {}", name, paths);
        return paths;
    }

    indexer_map default_list(const std::string &data_dir, scheduler &sched)
    {
        const auto idx_dir = incremental::storage_dir(data_dir);
        indexer_map indexers {};
        indexers.emplace(std::make_shared<index::stake_ref::indexer>(idx_dir, index_layout_t::stake_ref, sched));
        indexers.emplace(std::make_shared<index::pay_ref::indexer>(idx_dir, index_layout_t::pay_ref, sched));
        indexers.emplace(std::make_shared<index::tx::indexer>(idx_dir, index_layout_t::tx, sched));
        indexers.emplace(std::make_shared<index::txo_use::indexer>(idx_dir, index_layout_t::txo_use, sched));
        return indexers;
    }
}
