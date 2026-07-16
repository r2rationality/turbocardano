/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/scheduler.hpp>
#include <turbo/cardano.hpp>
#include <turbo/chunk-registry.hpp>
#include <turbo/index/pay-ref.hpp>
#include <turbo/index/stake-ref.hpp>
#include <turbo/index/tx.hpp>
#include <turbo/index/txo-use.hpp>
#include <turbo/indexer.hpp>

namespace turbo::indexer {
    struct incremental::impl {
        impl(chunk_registry &cr, indexer_map &&indexers)
            : _cr { cr }, _indexers { std::move(indexers) }, _idx_dir { storage_dir(_cr.data_dir().string()) },
                _index_state_path { (_idx_dir / "state.json").string() },
                _index_state_pre_path { (_idx_dir / "state-pre.json").string() },
                _mergeable { _mergeable_indexers(_indexers) }
        {
            file::set_max_open_files();
            for (auto &[name, idxr_ptr]: _indexers) {
                idxr_ptr->clean_up();
            }
            uint64_t end_offset = 0;
            if (std::filesystem::exists(_index_state_path)) {
                auto j_slices = json::load(_index_state_path).as_array();
                bool stop = false;
                for (auto &j: j_slices) {
                    auto slice = merger::slice::from_json(j.as_object());
                    for (auto &[name, idxr_ptr]: _indexers) {
                        if (idxr_ptr->mergeable() && !idxr_ptr->exists(slice.slice_id)) {
                            logger::warn("missing slice {} of index {} - truncating to the previous slice", slice.slice_id, name);
                            stop = true;
                            break;
                        }
                    }
                    if (stop)
                        break;
                    if (slice.offset != end_offset) {
                        logger::warn("offset of slice {} is not continuous - truncating to the previous slice", slice.slice_id);
                        break;
                    }
                    _slices.add(std::move(slice));
                    end_offset = slice.offset + slice.size;
                }
            }
            if (end_offset != indexed_bytes())
                throw error(fmt::format("internal error: indexed size calculation is incorrect: {} vs {}", end_offset, indexed_bytes()));
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
            return indexer::multi_reader_paths(_idx_dir.string(), name, slcs);
        }

        slice_path_list reader_paths(const std::string &name) const
        {
            return indexer::multi_reader_paths(_idx_dir.string(), name, slices());
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

        chunk_indexer_list make_chunk_indexers(uint64_t chunk_offset)
        {
            chunk_indexer_list chunk_indexers {};
            for (auto &[name, idxr_ptr]: _indexers)
                chunk_indexers.emplace_back(idxr_ptr->make_chunk_indexer("update", chunk_offset));
            return chunk_indexers;
        }
    private:
        chunk_registry &_cr;
        const indexer_map _indexers;
        const std::filesystem::path _idx_dir;
        const std::string _index_state_path;
        const std::string _index_state_pre_path;
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
            [this] { return _idx_end_offset(); },
            [this] { _idx_start_tx(); },
            [this] { _idx_prepare_tx(); },
            [this] { _idx_rollback_tx(); },
            [this] { _idx_commit_tx(); },
            [this](const auto &new_tip, const auto track) { _idx_truncate(new_tip, track); },
            {},
            {},
            [this](const auto epoch, const auto &info) { _idx_on_epoch_update(epoch, info); }
        };

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
                const auto output_path = idxr_ptr->reader_path(output_slice.slice_id);
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
            if (!epoch_slices_lk)
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
                if (total_size < merger::part_size && !force/* && _merge_next_offset + total_size < _cr.tx()->target_offset()*/)
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
                        _slices.add(output_slice);
                        const auto new_indexed_size = _slices.continuous_size();
                        if (new_indexed_size > old_indexed_size) {
                            for (auto slice_it = _slices.find(old_indexed_size); slice_it != _slices.end() && slice_it->second.end_offset() <= new_indexed_size; ++slice_it)
                                notify_slices.emplace_back(slice_it->second);
                        }
                        merged_max_slot = _slices.continuous_max_slot();
                        merged_end_offset = _slices.continuous_size();
                        // experimental support for on-the-go checkpoints
                        _save_json_slices(_index_state_path);
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
                    _slices.add(new_slice);
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
            _save_json_slices(_index_state_pre_path);
        }

        void _idx_rollback_tx()
        {
            for (const auto &s: _slices_added) {
                _slices.erase(s.offset);
                for (auto &[name, idxr_ptr]: _indexers)
                    _cr.remover().mark(idxr_ptr->reader_path(s.slice_id));
            }
            _slices_added.clear();
            for (const auto &s: _slices_truncated) {
                _slices.add(s);
            }
            _slices_truncated.clear();
        }

        void _idx_commit_tx()
        {
            if (!std::filesystem::exists(_index_state_pre_path))
                throw error(fmt::format("the prepared chunk_registry state file is missing: {}!", _index_state_pre_path));
            std::filesystem::rename(_index_state_pre_path, _index_state_path);
            for (const auto &s: _slices_truncated) {
                for (auto &[name, idxr_ptr]: _indexers)
                    _cr.remover().mark(idxr_ptr->reader_path(s.slice_id));
            }
            _slices_truncated.clear();
            _slices_added.clear();
        }

        void _idx_on_chunk_add(const storage::chunk_info &chunk, const cardano::parsed_block_list &blocks) const
        {
            chunk_indexer_list chunk_indexers {};
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

    incremental::incremental(chunk_registry &cr, indexer_map &&indexers)
        : _impl { std::make_unique<impl>(cr, std::move(indexers)) }
    {
    }

    incremental::~incremental() =default;

    chunk_indexer_list incremental::make_chunk_indexers(const uint64_t chunk_offset)
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
        indexers.emplace(std::make_shared<index::stake_ref::indexer>(idx_dir, "stake-ref", sched));
        indexers.emplace(std::make_shared<index::pay_ref::indexer>(idx_dir, "pay-ref", sched));
        indexers.emplace(std::make_shared<index::tx::indexer>(idx_dir, "tx", sched));
        indexers.emplace(std::make_shared<index::txo_use::indexer>(idx_dir, "txo-use", sched));
        return indexers;
    }
}