/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/cardano.hpp>
#include <dt/cardano/ledger/state.hpp>
#include <dt/chunk-registry.hpp>

namespace daedalus_turbo {
    chunk_registry::chunk_registry(const std::string &data_dir, const mode mode,
        cardano::config ccfg, scheduler &sched, file_remover &fr, const bool auto_maintenance)
        : _data_dir { data_dir }, _db_dir { init_db_dir((_data_dir / "compressed").string()) },
            _cardano_cfg { std::move(ccfg) }, _sched { sched }, _file_remover { fr },
            _state_path { (_db_dir / "state.bin").string() },
            _state_path_pre { (_db_dir / "state-pre.bin").string() }
    {
        timer t { "chunk-registry construct" };
        switch (mode) {
            case mode::validate:
                _indexer = std::make_unique<indexer::incremental>(*this, validator::default_indexers(_data_dir.string(), _sched));
                _validator = std::make_unique<validator::incremental>(*this);
                break;
            case mode::index:
                _indexer = std::make_unique<indexer::incremental>(*this, indexer::default_list(_data_dir.string(), _sched));
                break;
            case mode::store:
                // do nothing
                break;
            default:
                throw error(fmt::format("unsupported mode: {}", static_cast<int>(mode)));
        }
        file_set known_chunks {}, deletable_chunks {};
        chunk_map chunks {};
        if (std::filesystem::exists(_state_path))
            zpp::load(chunks, _state_path);
        for (auto &&[last_byte_offset, chunk]: chunks) {
            const auto path = full_path(chunk.rel_path());
            std::error_code ec {};
            uint64_t file_size = std::filesystem::file_size(path, ec);
            if (ec) {
                logger::info("load_state: file access error for {}: {} - ignoring it and the following chunks!",
                    chunk.rel_path(), ec.message());
                break;
            }
            if (file_size != chunk.compressed_size) {
                logger::info("load_state: file size mismatch for {}: recorded: {} vs actual: {}: ignoring it and the following chunks!",
                    chunk.rel_path(), chunk.compressed_size, file_size);
                break;
            }
            _add(std::move(chunk), false);
            known_chunks.emplace(std::move(path));
        }
        for (const auto &entry: std::filesystem::recursive_directory_iterator { _db_dir }) {
            auto path = full_path(entry.path().string());
            if (entry.is_regular_file() && entry.path().extension() == ".zstd" && !known_chunks.contains(path))
                _file_remover.mark(path);
        }
        logger::info("chunk_registry has data up to offset {}", num_bytes());
        if (auto_maintenance)
            _maintenance();
    }

    chunk_registry::~chunk_registry() =default;

    void chunk_registry::register_processor(const chunk_processor &p)
    {
        _processors.emplace(&p);
    }

    void chunk_registry::remove_processor(const chunk_processor &p)
    {
        _processors.erase(&p);
    }

    void chunk_registry::report_progress(const std::string_view name, const progress_point &tip) const
    {
        if (_transaction) [[likely]] {
            uint64_t rel_pos = 0;
            uint64_t rel_target = 0;
            // prefer to compute the progress using offsets, fallback to slots if not available
            if (_transaction->target->end_offset) {
                rel_pos = tip.end_offset;
                rel_target = _transaction->target->end_offset;
                if (_transaction->start) {
                    rel_pos -= _transaction->start->end_offset;
                    rel_target -= _transaction->start->end_offset;
                }
            } else {
                rel_pos = tip.slot;
                rel_target = _transaction->target->slot;
                if (_transaction->start) {
                    rel_pos -= _transaction->start->slot;
                    rel_target -= _transaction->start->slot;
                }
            }
            uint64_t prev_pos = 0;
                {
                mutex::scoped_lock lk { _tx_progress_mutex };
                if (const auto [it, created] = _tx_progress_max.try_emplace(std::string { name }, rel_pos); !created) {
                    prev_pos = it->second;
                    if (it->second < rel_pos)
                        it->second = rel_pos;
                }
                }
            if (prev_pos < rel_pos) {
                for (const auto *p: _processors) {
                    if (p->on_progress)
                        p->on_progress(name, rel_pos, rel_target);
                }
            }
        } else {
            throw error("report_progress can be called only inside of a transaction");
        }
    }

    void chunk_registry::_maintenance()
    {
        if (valid_end_offset() != max_end_offset()) {
            logger::warn("the local chain is not in a consistent state, performing maintenance ...");
            truncate(tip());
            remover().remove();
        } else {
            logger::info("the local chain is in a consistent state");
        }
    }

    void chunk_registry::validation_failure_handler(const std::function<void(uint64_t)> &handler)
    {
        _sched.on_result(std::string { validator::validate_leaders_task }, [handler](auto &&res) {
            if (res.type() == typeid(scheduled_task_error)) {
                const auto task = std::any_cast<scheduled_task_error>(std::move(res)).task();
                handler(std::any_cast<chunk_offset_t>(*task.param));
            }
      });
    }

    const indexer::incremental &chunk_registry::indexer() const
    {
        if (_indexer) [[likely]]
            return *_indexer;
        throw error("This chunk_registry does not have an indexer instance!");
    }

    const validator::incremental &chunk_registry::validator() const
    {
        if (_validator) [[likely]]
            return *_validator;
        throw error("This chunk_registry does not have a validator instance!");
    }

    cardano::amount chunk_registry::unspent_reward(const cardano::stake_ident &id) const
    {
        if (_validator) [[likely]]
            return _validator->unspent_reward(id);
        throw error("This chunk_registry does not have a validator instance!");
    }

    cardano::tail_relative_stake_map chunk_registry::tail_relative_stake() const
    {
        if (_validator) [[likely]]
            return _validator->tail_relative_stake();
        throw error("This chunk_registry does not have a validator instance!");
    }

    cardano::optional_point chunk_registry::tip() const
    {
        if (const auto last_block = last_valid_block(); last_block) [[likely]]
            return cardano::point { last_block->hash, last_block->slot, last_block->height, last_block->end_offset() };
        return {};
    }

    cardano::optional_point chunk_registry::core_tip() const
    {
        return validator().core_tip();
    }

    cardano::optional_point chunk_registry::immutable_tip() const
    {
        if (!_chunks.empty()) {
            size_t blocks_after = 0;
            for (const auto &[last_byte, chunk]: _chunks | std::ranges::views::reverse) {
                if (blocks_after >= _cardano_cfg.shelley_security_param)
                    return chunk.blocks.back().point();
                blocks_after += chunk.num_blocks;
            }
        }
        return {};
    }

    std::string chunk_registry::node_export_ledger(const std::filesystem::path &ledger_dir, const cardano::optional_point &imm_tip, const int prio) const
    {
        if (_validator) [[likely]] {
            if (imm_tip && _validator->can_export(imm_tip)) {
                std::filesystem::create_directories(ledger_dir);
                return _validator->node_export(ledger_dir, imm_tip, prio);
            }
            throw error("the validator's state is currently not in the exportable period!");
        }
        throw error("This chunk_registry does not have a validator instance!");
    }

    std::optional<storage::block_info> chunk_registry::latest_block_after_or_at_slot(const uint64_t slot) const
    {
        for (auto chunk_it = _find_chunk_by_slot(slot); chunk_it != _chunks.end(); ++chunk_it) {
            for (auto block_it = chunk_it->second.blocks.begin(); block_it != chunk_it->second.blocks.end(); ++block_it) {
                if (block_it->slot >= slot)
                    return *block_it;
            }
        }
        return {};
    }

    std::optional<storage::block_info> chunk_registry::latest_block_before_or_at_slot(const uint64_t slot) const
    {
        if (!_chunks.empty()) {
            auto chunk_it = _find_chunk_by_slot(slot);
            if (chunk_it == _chunks.end())
                --chunk_it;
            for (;;) {
                for (auto block_it = chunk_it->second.blocks.rbegin(); block_it != chunk_it->second.blocks.rend(); ++block_it) {
                    if (block_it->slot <= slot)
                        return *block_it;
                }
                if (chunk_it == _chunks.begin())
                    break;
                --chunk_it;
            }
        }
        return {};
    }

    void chunk_registry::_node_export_chain(const std::filesystem::path &immutable_dir, const std::filesystem::path &volatile_dir, const int prio_base) const
    {
        // chunk registry may store the same Cardano Node chunk in multiple files, so need to combine them for the export
        struct merged_chunk {
            uint64_t first_slot = 0;
            uint64_t last_slot = 0;
            vector<std::string> files {};
            vector<const storage::block_info *> blocks {};
        };
        using merged_chunk_map = map<uint64_t, merged_chunk>;

        std::filesystem::remove_all(immutable_dir);
        std::filesystem::create_directories(immutable_dir);
        const auto done_bytes = std::make_shared<std::atomic_uint64_t>(0);
        const auto total_bytes = num_bytes();

        // split chunks into volatile and immutable ones
        vector<const storage::chunk_info *> volatile_chunks {};
        merged_chunk_map immutable_chunks {};
        const auto imm_tip = immutable_tip();
        for (const auto &[last_byte, chunk]: chunks()) {
            if (imm_tip < chunk.blocks.back().point()) {
                volatile_chunks.emplace_back(&chunk);
            } else {
                const auto chunk_id = make_slot(chunk.first_slot).chunk_id();
                const auto chunk_path = full_path(chunk.rel_path());
                const auto [it, created] = immutable_chunks.try_emplace(chunk_id, chunk.first_slot);
                it->second.last_slot = chunk.last_slot;
                it->second.files.emplace_back(chunk_path);
                for (const auto &block: chunk.blocks)
                    it->second.blocks.emplace_back(&block);
            }
        }

        logger::info("exporting chunks to {} immutable: {} volatile: {}", immutable_dir.string(), immutable_chunks.size(), volatile_chunks.size());
        // export immutable chunks
        for (const auto &[chunk_id, m_chunk]: immutable_chunks) {
            _sched.submit_void("decompress", prio_base, [this, done_bytes, total_bytes, chunk_id, m_chunk, immutable_dir, imm_tip] {
                const auto data_path = (immutable_dir / fmt::format("{:05}.chunk", chunk_id)).string();
                const auto pri_path = (immutable_dir / fmt::format("{:05}.primary", chunk_id)).string();
                const auto sec_path = (immutable_dir / fmt::format("{:05}.secondary", chunk_id)).string();
                const auto chunk_start_slot = cardano::slot::from_chunk(chunk_id, _cardano_cfg);
                const uint64_t chunk_start_offset = m_chunk.blocks.front()->offset;
                uint64_t chunk_max_slot = _cardano_cfg.byron_slots_per_chunk;
                if (imm_tip && imm_tip->slot - chunk_start_slot < chunk_max_slot)
                    chunk_max_slot = imm_tip->slot - chunk_start_slot;
                uint64_t data_size = 0;
                {
                    logger::debug("writing chunk {}", data_path);
                    uint8_vector data {};
                    for (const auto &path: m_chunk.files)
                        data << file::read_auto(path);
                    file::write(data_path, data);
                    data_size = data.size();
                }
                {
                    file::write_stream pri_ws { pri_path };
                    file::write_stream sec_ws { sec_path };
                    pri_ws.write(buffer::from<uint8_t>(1));
                    uint32_t next_block_offset = 0;
                    uint32_t next_rel_slot = 0;
                    for (const auto *blk: m_chunk.blocks) {
                        if (blk->slot < chunk_start_slot) [[unlikely]]
                            throw error(fmt::format("block with slot {} must not be in chunk {}!", blk->slot, chunk_id));
                        const auto blk_rel_slot = blk->era > 0 ? blk->slot - chunk_start_slot + 1 : 0;
                        for (; next_rel_slot <= blk_rel_slot; ++next_rel_slot)
                            pri_ws.write(buffer::from(host_to_net<uint32_t>(next_block_offset)));
                        if (blk->offset < chunk_start_offset) [[unlikely]]
                            throw error(fmt::format("block with offset {} must not be in chunk starting at offset {}!", blk->offset, chunk_start_offset));
                        const auto blk_rel_offset = blk->offset - chunk_start_offset;
                        sec_ws.write(buffer::from(host_to_net<uint64_t>(blk_rel_offset)));
                        sec_ws.write(buffer::from(host_to_net<uint16_t>(blk->header_offset)));
                        sec_ws.write(buffer::from(host_to_net<uint16_t>(blk->header_size)));
                        sec_ws.write(buffer::from(host_to_net<uint32_t>(blk->chk_sum)));
                        sec_ws.write(blk->hash);
                        //store 0 instead of blk.height for byte-for-byte compatibility with Cardano Node
                        sec_ws.write(buffer::from(host_to_net<uint32_t>(0)));
                        sec_ws.write(buffer::from(host_to_net<uint32_t>(blk->era > 0 ? blk->slot : chunk_start_slot.epoch())));
                        next_block_offset += 56;
                        next_rel_slot = blk_rel_slot + 1;
                    }
                    for (; next_rel_slot <= chunk_max_slot; ++next_rel_slot)
                        pri_ws.write(buffer::from(host_to_net<uint32_t>(next_block_offset)));
                    pri_ws.write(buffer::from(host_to_net<uint32_t>(next_block_offset)));
                }
                const auto new_done_blocks = done_bytes->fetch_add(data_size, std::memory_order_relaxed) + data_size;
                progress::get().update("chunk-export", new_done_blocks, total_bytes);
            });
        }

        // export volatile chunks
        {
            std::filesystem::remove_all(volatile_dir);
            std::filesystem::create_directories(volatile_dir);
            static constexpr size_t max_volatile_file_blocks = 1000;
            uint8_vector volatile_data {};
            vector<size_t> volatile_block_sizes {};
            for (const auto *chunk_ptr: volatile_chunks) {
                volatile_data << file::read_auto(full_path(chunk_ptr->rel_path()));
                for (const auto &block: chunk_ptr->blocks)
                    volatile_block_sizes.emplace_back(block.size);
            }
            uint64_t volatile_offset = 0;
            uint64_t volatile_file_no = 0;
            for (size_t bi = 0; bi < volatile_block_sizes.size(); bi += max_volatile_file_blocks) {
                const uint64_t start_offset = volatile_offset;
                uint64_t file_size = 0;
                const auto batch_end = std::min(volatile_block_sizes.size(), bi + max_volatile_file_blocks);
                for (size_t i = bi; i < batch_end; ++i) {
                    file_size += volatile_block_sizes[i];
                }
                file::write(
                    (volatile_dir / fmt::format("blocks-{}.dat", volatile_file_no)).string(),
                    static_cast<buffer>(volatile_data).subbuf(start_offset, file_size));
                volatile_offset += file_size;
                ++volatile_file_no;
                const auto new_done_blocks = done_bytes->fetch_add(file_size, std::memory_order_relaxed) + file_size;
                progress::get().update("chunk-export", new_done_blocks, total_bytes);
            }
        }
    }

    void chunk_registry::node_export(const std::filesystem::path &node_dir, const cardano::point &tip, const bool ledger_only) const
    {
        progress_guard pg { "chunk-export", "ledger-export" };
        logger::debug("node_export started to {}", node_dir.string());
        const auto ex_ptr = logger::run_log_errors([&] {
            node_export_ledger(std::filesystem::weakly_canonical(node_dir / "ledger"), tip);
            if (!ledger_only) {
                std::filesystem::remove(node_dir / "clean");
                _node_export_chain(std::filesystem::weakly_canonical(node_dir / "immutable").string(),
                    std::filesystem::weakly_canonical(node_dir / "volatile").string(), 100);
                std::filesystem::remove(node_dir / "lock");
                file::write((node_dir / "protocolMagicId").string(), fmt::format("{}", _cardano_cfg.byron_protocol_magic));
                file::write((node_dir / "clean").string(), std::string_view { "" });
            }
        });
        if (ex_ptr)
            _sched.cancel([](const auto &, const auto &) { return true; });
        _sched.process(true);
        if (ex_ptr)
            std::rethrow_exception(ex_ptr);
    }

    cardano::optional_slot chunk_registry::can_export() const
    {
        return validator().can_export(immutable_tip());
    }

    void chunk_registry::_add(chunk_info &&chunk, const bool normal)
    {
        if (normal && _transaction->target_slot() < chunk.last_slot)
            throw error(fmt::format("chunk's data exceeds the target slot: {}", _transaction->target_slot()));
        if (chunk.data_size == 0 || chunk.num_blocks == 0 || chunk.blocks.empty())
            throw error(fmt::format("chunk at offset {} is empty!", chunk.offset));
        mutex::unique_lock update_lk { _update_mutex };
        auto [um_it, um_created] = _unmerged_chunks.try_emplace(chunk.offset + chunk.data_size - 1, std::move(chunk));
        // chunk variable should not be used after this point due to std::move(chunk) right above
        if (!um_created)
            throw error(fmt::format("internal error: duplicate chunk offset: {} size: {}", um_it->second.offset, um_it->second.data_size));
        while (!_unmerged_chunks.empty() && _unmerged_chunks.begin()->second.offset == num_bytes()) {
            const auto &tested_chunk = _unmerged_chunks.begin()->second;
            if (const auto &first_block = tested_chunk.blocks.at(0); first_block.era >= 2 && !_cardano_cfg.shelley_started()) {
                // If there were no blocks before this one, then count from the slot 0
                _cardano_cfg.shelley_start_epoch(_chunks.empty() ? 0 : first_block.slot / _cardano_cfg.byron_epoch_length);
            }
            if (_validator) {
                if (const auto future_slot = cardano::slot::from_future(_cardano_cfg); tested_chunk.last_slot >= future_slot)
                    throw error(fmt::format("a chunk with its last block with a time slot from the future: {}!", tested_chunk.last_slot));
                if (!_chunks.empty()) {
                    const auto &last = _chunks.rbegin()->second;
                    if (tested_chunk.first_slot < last.last_slot)
                        throw error(fmt::format("chunk at offset {} has its first slot {} less than the last slot in the registry {}",
                            tested_chunk.offset, tested_chunk.first_slot, last.last_slot));
                    if (last.last_block_hash != tested_chunk.prev_block_hash)
                        throw error(fmt::format("chunk at offset {}: prev_block_hash {} does not match the prev chunk's last_block_hash of the last block {}",
                            tested_chunk.offset, tested_chunk.prev_block_hash, last.last_block_hash));
                } else {
                    if (tested_chunk.prev_block_hash != _cardano_cfg.byron_genesis_hash)
                        throw error(fmt::format("chunk at offset {}: prev_block_hash {} does not match the genesis hash {}",
                            tested_chunk.offset, tested_chunk.prev_block_hash, _cardano_cfg.byron_genesis_hash));
                }
            }
            const auto first_slot = make_slot(tested_chunk.first_slot);
            const auto last_slot = make_slot(tested_chunk.last_slot);
            if (first_slot.epoch() != last_slot.epoch())
                throw error(fmt::format("chunk at offset {} contains blocks from multiple epochs: first slot: {} last_slot: {}", tested_chunk.offset, first_slot, last_slot));
            if (first_slot.chunk_id() != last_slot.chunk_id())
                throw error(fmt::format("chunk at offset {} contains blocks from multiple chunks: {} and {}", tested_chunk.offset, first_slot.chunk_id(), last_slot.chunk_id()));
            auto [it, created, node] = _chunks.insert(_unmerged_chunks.extract(_unmerged_chunks.begin()));
            const auto &inserted_chunk = it->second;
            if (!created)
                throw error(fmt::format("internal error: duplicate chunk offset: {} size: {}", inserted_chunk.offset, inserted_chunk.data_size));
        }
        if (normal)
            _notify_of_updates(update_lk);
    }

    std::pair<storage::chunk_info, std::exception_ptr> chunk_registry::_parse(const uint64_t offset, const buffer &raw_data, const size_t compressed_size) const
    {
        chunk_info chunk { .data_size=raw_data.size(), .compressed_size=compressed_size, .offset=offset };
        std::exception_ptr ex_ptr {};
        uint8_vector ok_data {};
        uint64_t prev_slot = 0;
        std::optional<indexer::chunk_indexer_list> chunk_indexers {};
        if (_indexer)
            chunk_indexers = _indexer->make_chunk_indexers(offset);
        cbor::zero2::decoder dec { raw_data };
        while (!dec.done()) {
            try {
                auto &block_tuple = dec.read();
                const cardano::block_container blk_ptr { numeric_cast<uint64_t>(chunk.offset + block_tuple.data_begin() - raw_data.data()), block_tuple, _cardano_cfg };
                {
                    const auto &blk = *blk_ptr;
                    const auto slot = blk.slot();
                    if (slot < prev_slot)
                        throw error(fmt::format("chunk at {}: a block's slot {} is less than the slot of the prev block {}!", offset, slot, prev_slot));
                    prev_slot = slot;
                    static constexpr auto max_era = std::numeric_limits<uint8_t>::max();
                    if (blk.era() > max_era)
                        throw error(fmt::format("block at slot {} has era {} that is outside of the supported max limit of {}", slot, blk.era(), max_era));
                    static constexpr auto max_size = std::numeric_limits<uint32_t>::max();
                    if (blk_ptr.raw().size() > max_size)
                        throw error(fmt::format("block at slot {} has size {} that is outside of the supported max limit of {}", slot, blk_ptr.raw().size(), max_size));
                    if (!chunk.blocks.empty()) {
                        if (_validator && blk.prev_hash() != chunk.last_block_hash)
                            throw error(fmt::format("block at slot {} has an inconsistent prev_hash {}", blk.slot(), blk.prev_hash()));
                    } else {
                        chunk.prev_block_hash = blk.prev_hash();
                        chunk.first_slot = slot;
                    }
                    for (const auto *p: _processors) {
                        if (p->on_block_validate)
                            p->on_block_validate(blk);
                    }
                    chunk.last_block_hash = blk.hash();
                    chunk.last_slot = slot;
                    if (chunk_indexers) {
                        for (auto &idxr: *chunk_indexers)
                            idxr->index(blk_ptr);
                        blk.foreach_tx([&](const auto &tx) {
                            for (auto &idxr: *chunk_indexers)
                                idxr->index_tx(tx);
                        });
                        blk.foreach_invalid_tx([&](const auto &tx) {
                            for (auto &idxr: *chunk_indexers)
                                idxr->index_invalid_tx(tx);
                        });
                    }
                    chunk.blocks.emplace_back(storage::block_info::from_block(blk_ptr));
                    ok_data << blk_ptr.raw();
                }
            } catch (...) {
                ex_ptr = std::current_exception();
                break;
            }
        }

        // happens if some blocks are valid but others are invalid
        blake2b(chunk.data_hash, ok_data);
        chunk.num_blocks = chunk.blocks.size();
        if (ok_data.size() != raw_data.size()) {
            chunk.data_size = ok_data.size();
            const auto compressed = zstd::compress(ok_data);
            chunk.compressed_size = compressed.size();
            file::write(full_path(chunk.rel_path()), compressed);
        }
        for (const auto *p: _processors) {
            if (p->on_chunk_add)
                p->on_chunk_add(chunk);
        }
        // chunks can be parsed out of order so in the end offset we report the number of parsed bytes
        // rather than the last parsed offset as this better reflects the progress made
        const auto num_parsed = _tx_progress_parse.fetch_add(chunk.data_size, std::memory_order_relaxed) + chunk.data_size;
        report_progress("parse", { chunk.last_slot,  _transaction->start_offset() + num_parsed });
        return std::make_pair(std::move(chunk), std::move(ex_ptr));
    }

    buffer chunk_registry::const_iterator::_prep_chunk_cache() const
    {
        const auto rel_path = _chunk_it->second.rel_path();
        const auto path = _cr.full_path(_chunk_it->second.rel_path());
        if (!_chunk_cache || _chunk_cache->full_path != path || _chunk_it->second.data_size != _chunk_cache->data.size()) {
            _chunk_cache.emplace(path, zstd::read(path));
        }
        return _chunk_cache->data;
    }

    cardano::parsed_header chunk_registry::const_iterator::header() const
    {
        const auto bytes = _prep_chunk_cache();
        const auto &blk = operator*();
        // + 1 is the offset of the first entry in the block array which is the header
        const auto file_offset = blk.offset + blk.header_offset + 1 - _chunk_it->second.offset;
        return { blk.era, static_cast<buffer>(bytes).subbuf(file_offset,  blk.header_size), _cr.config() };
    }

    uint8_vector chunk_registry::const_iterator::block_data() const
    {
        const auto bytes = _prep_chunk_cache();
        const auto &blk = operator*();
        // + 1 is the offset of the first entry in the block array which is the header
        const auto file_offset = blk.offset - _chunk_it->second.offset;
        return { bytes.subbuf(file_offset,  blk.size) };
    }

    std::pair<uint8_vector, chunk_registry::const_iterator>
    chunk_registry::const_iterator::chunk_remaining_data(const const_iterator last_it) const
    {
        if (*this == last_it) [[unlikely]]
            return std::make_pair(uint8_vector {}, last_it);
        if (**this == _chunk_it->second.blocks.front() && last_it._chunk_it != _chunk_it) {
            const auto path = _cr.full_path(_chunk_it->second.rel_path());
            return std::make_pair(file::read<uint8_vector>(path), const_iterator { _cr, std::next(_chunk_it), 0 });
        }
        const auto bytes = _prep_chunk_cache();
        const auto &blk = operator*();
        const auto file_offset = blk.offset - _chunk_it->second.offset;
        if (last_it._chunk_it == _chunk_it && last_it._block_no < _chunk_it->second.blocks.size()) {
            return std::make_pair(
                zstd::compress(bytes.subbuf(file_offset, last_it->offset - blk.offset), 3),
                last_it
            );
        }
        return std::make_pair(
            zstd::compress(bytes.subbuf(file_offset), 3),
            const_iterator { _cr, std::next(_chunk_it), 0 }
        );
    }
}
