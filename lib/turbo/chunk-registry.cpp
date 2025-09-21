/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cardano.hpp>
#include <turbo/cardano/ledger/state.hpp>
#include <turbo/chunk-registry.hpp>

namespace turbo {
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
            maintenance();
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

    void chunk_registry::maintenance()
    {
        if (valid_end_offset() != max_end_offset()) {
            logger::warn("the local chain is not in a consistent state, performing maintenance ...");
            truncate(tip());
            _file_remover.remove();
        } else {
            logger::info("the local chain is in a consistent state");
        }
    }

    void chunk_registry::validation_failure_handler(const std::function<void(uint64_t)> &handler)
    {
        static const std::vector<std::string> task_names{
            std::string{validator::validate_leaders_task},
            std::string{validator::validate_task}
        };
        const auto internal_handler = [handler](const scheduled_task_error &err) {
            const chunk_offset_t *offset_ptr = err.task().param ? std::any_cast<chunk_offset_t>(&*err.task().param) : nullptr;
            const auto offset = offset_ptr ? *offset_ptr : 0U;
            logger::debug("chunk_registry::validation_failure_handler at offset {}: {}", offset, err.what());
            handler(offset);
        };
        for (const auto &task: task_names)
            _sched.on_error(task, internal_handler, true);
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

    std::optional<chunk_registry::active_transaction> chunk_registry::tx() const
    {
        return _transaction;
    }

    const cardano::config &chunk_registry::config() const
    {
        return _cardano_cfg;
    }

    scheduler &chunk_registry::sched() const
    {
        return _sched;
    }

    file_remover &chunk_registry::remover() const
    {
        return _file_remover;
    }

    bool chunk_registry::empty() const
    {
        return cbegin() == cend();
    }

    chunk_registry::const_iterator chunk_registry::cbegin() const
    {
        return const_iterator::cbegin(*this, _chunks);
    }

    chunk_registry::const_iterator chunk_registry::cend() const
    {
        return const_iterator::cend(*this, _chunks);
    }

    chunk_registry::const_reverse_iterator chunk_registry::crbegin() const
    {
        return { cend() };
    }

    chunk_registry::const_reverse_iterator chunk_registry::crend() const
    {
        return { cbegin() };
    }

    const chunk_registry::chunk_map &chunk_registry::chunks() const
    {
        return _chunks;
    }

    epoch_map chunk_registry::epochs() const
    {
        mutex::scoped_lock lk { _update_mutex };
        epoch_map eps {};
        std::optional<uint64_t> last_epoch {};
        epoch_info::chunk_list chunks {};
        for (const auto &[last_byte_offset, chunk]: _chunks) {
            const auto chunk_epoch = make_slot(chunk.first_slot).epoch();
            if (!last_epoch || *last_epoch != chunk_epoch) {
                if (last_epoch && !chunks.empty())
                    eps.try_emplace(*last_epoch, std::move(chunks));
                last_epoch = chunk_epoch;
                chunks.clear();
            }
            chunks.emplace_back(&chunk);
        }
        if (last_epoch && !chunks.empty())
            eps.try_emplace(*last_epoch, std::move(chunks));
        return eps;
    }

    bool chunk_registry::has_epoch(const uint64_t epoch) const
    {
        mutex::unique_lock lk { _update_mutex };
        return _has_epoch(epoch, lk);
    }

    cardano::slot chunk_registry::make_slot(uint64_t slot_) const
    {
        return { slot_, _cardano_cfg };
    }

    uint64_t chunk_registry::num_bytes() const
    {
        if (!_chunks.empty()) [[likely]]
            return _chunks.rbegin()->second.end_offset();
        return 0;
    }

    uint64_t chunk_registry::num_compressed_bytes() const
    {
        if (!_chunks.empty()) [[likely]]
            return std::accumulate(_chunks.begin(), _chunks.end(), 0ULL,
                [](auto sum, const auto &chunk) { return sum + chunk.second.compressed_size; });
        return 0;
    }

    size_t chunk_registry::num_blocks() const
    {
        return std::accumulate(_chunks.begin(), _chunks.end(), static_cast<size_t>(0),
            [](auto sum, const auto &val) { return sum + val.second.blocks.size(); });
    }

    chunk_registry::const_iterator chunk_registry::find_by_offset(const uint64_t offset) const
    {
        if (const auto chunk_it = _find_chunk_by_offset(offset); chunk_it != _chunks.end()) {
            if (const auto block_it = _find_block_by_offset(chunk_it, offset); block_it != chunk_it->second.blocks.end()) {
                if (offset >= block_it->offset && offset < block_it->offset + block_it->size)
                    return { *this, _chunks, chunk_it, numeric_cast<size_t>(block_it - chunk_it->second.blocks.begin()) };
                throw error("internal error: block metadata does not match the transaction!");
            }
        }
        return const_iterator::cend(*this, _chunks);
    }

    std::optional<storage::block_info> chunk_registry::find_block_by_offset_no_throw(const uint64_t offset) const
    {
        if (const auto it = find_by_offset(offset); it != cend())
            return *it;
        return {};
    }

    storage::block_info chunk_registry::find_block_by_offset(const uint64_t offset) const
    {
        if (const auto block = find_block_by_offset_no_throw(offset))
            return *block;
        throw error(fmt::format("unknown offset: {}!", offset));
    }

    const storage::block_info &chunk_registry::find_block_by_slot(const uint64_t slot) const
    {
        const auto chunk_it = _find_chunk_by_slot(slot);
        if (chunk_it == _chunks.end())
            throw error(fmt::format("internal error: no block registered at a slot: {}!", slot));
        const auto block_it = _find_block_by_slot(chunk_it, slot);
        if (block_it == chunk_it->second.blocks.end())
            throw error(fmt::format("internal error: no block registered at a slot: {}!", slot));
        if (block_it->slot != slot)
            throw error(fmt::format("internal error: no block registered at a slot: {}!", slot));
        return *block_it;
    }

    chunk_registry::const_iterator chunk_registry::find_block(const cardano::point2 &p) const
    {
        if (auto chunk_it = _find_chunk_by_slot(p.slot); chunk_it != _chunks.end()) [[likely]] {
            if (auto block_it = _find_block_by_slot(chunk_it, p.slot); block_it != chunk_it->second.blocks.end()) [[likely]] {
                if (block_it->slot == p.slot) {
                    for (;;) {
                        if (block_it->hash == p.hash) [[likely]]
                            return { *this, _chunks, chunk_it, numeric_cast<size_t>(block_it - chunk_it->second.blocks.begin()) };
                        if (++block_it == chunk_it->second.blocks.end()) [[unlikely]] {
                            if (++chunk_it == _chunks.end()) [[unlikely]]
                                break;
                            block_it = chunk_it->second.blocks.begin();
                        }
                        if (block_it->slot != p.slot)
                            break;
                    }
                }
            }
        }
        return cend();
    }

    storage::block_info chunk_registry::get_block_info(const cardano::point2 &p) const
    {
        if (const auto block_it = find_block(p); block_it != cend()) [[likely]]
            return *block_it;
        throw error(fmt::format("internal error: no such block: {}!", p));
    }

    uint64_t chunk_registry::find_epoch(const uint64_t offset) const
    {
        mutex::scoped_lock lk { _update_mutex };
        return make_slot(find_offset(offset).first_slot).epoch();
    }

    const chunk_registry::chunk_info &chunk_registry::find_offset(uint64_t offset) const
    {
        return _find_chunk_by_offset(offset)->second;
    }

    const chunk_registry::chunk_info &chunk_registry::find_last_block_hash(const buffer &last_block_hash) const
    {
        const auto it = std::find_if(_chunks.begin(), _chunks.end(),
                                     [&](const auto &el) { return el.second.last_block_hash == last_block_hash; });
        if (it == _chunks.end())
            throw error(fmt::format("there is no chunk with its last block hash {}", last_block_hash));
        return it->second;
    }

    chunk_registry::chunk_map::const_iterator chunk_registry::find_offset_it(uint64_t offset) const
    {
        return _find_chunk_by_offset(offset);
    }

    std::optional<storage::block_info> chunk_registry::last_valid_block() const
    {
        const auto end_offset = valid_end_offset();
        if (end_offset) [[likely]] {
            const auto chunk_it = _find_chunk_by_offset(end_offset - 1);
            if (chunk_it == _chunks.end()) [[unlikely]]
                throw error("internal error: chunk_registry state is inconsistent!");
            const auto block_it = _find_block_by_offset(chunk_it, end_offset - 1);
            if (block_it == chunk_it->second.blocks.end()) [[unlikely]]
                throw error("internal error: chunk_registry state is inconsistent!");
            return *block_it;
        }
        return {};
    }

    uint64_t chunk_registry::max_slot() const
    {
        if (!_chunks.empty()) [[likely]]
            return _chunks.rbegin()->second.last_slot;
        return 0;
    }

    uint64_t chunk_registry::valid_end_offset() const
    {
        uint64_t valid_end = _my_end_offset();
        for (const auto *p: _processors) {
            if (p->end_offset) {
                const auto proc_end = p->end_offset();
                if (proc_end < valid_end)
                    valid_end = proc_end;
            }
        }
        return valid_end;
    }

    uint64_t chunk_registry::max_end_offset() const
    {
        uint64_t max_end = _my_end_offset();
        for (const auto *p: _processors) {
            if (p->end_offset) {
                const auto proc_end = p->end_offset();
                if (proc_end > max_end)
                    max_end = proc_end;
            }
        }
        return max_end;
    }

    // block data access

    const std::filesystem::path &chunk_registry::data_dir() const
    {
        return _data_dir;
    }

    std::string chunk_registry::rel_path(const std::filesystem::path &full_path) const
    {
        return const_iterator::rel_path(_db_dir, full_path);
    }

    std::string chunk_registry::full_path(const std::filesystem::path &rel_path) const
    {
        return const_iterator::full_path(_db_dir, rel_path);
    }

    uint64_t chunk_registry::read_holding_chunk(uint8_vector &chunk_data, const uint64_t offset) const
    {
        if (offset >= num_bytes())
            throw error(fmt::format("the requested offset {} is larger than the maximum one: {}", offset, num_bytes()));
        const auto &chunk = find_offset(offset);
        if (offset >= chunk.offset + chunk.data_size)
            throw error("the requested chunk segment is too small to parse it");
        file::read_auto(full_path(chunk.rel_path()), chunk_data);
        return chunk.offset;
    }

    cbor::zero2::parsed_value chunk_registry::read_from_chunk_buffer(const uint64_t value_offset, const buffer &chunk_data, const uint64_t chunk_offset) const
    {
        if (value_offset < chunk_offset) [[unlikely]]
            throw error("the requested value offset is outside of the chunk's data range!");
        if (value_offset >= chunk_offset + chunk_data.size()) [[unlikely]]
            throw error("the requested chunk segment is too small to parse it");
        const size_t read_offset = value_offset - chunk_offset;
        const size_t read_size = chunk_data.size() - read_offset;
        return cbor::zero2::parse(chunk_data.subbuf(read_offset, read_size));
    }

    cbor::zero2::parsed_value chunk_registry::read(const uint64_t offset) const
    {
        const auto chunk_offset = read_holding_chunk(_read_buffer, offset);;
        return read_from_chunk_buffer(offset, _read_buffer, chunk_offset);
    }

    // state modifying methods

    void chunk_registry::import(const chunk_registry &src_cr)
    {
        uint8_vector raw_data {}, compressed_data {};
        _start_tx(tip(), src_cr.tip());
        for (const auto &[last_byte_offset, src_chunk]: src_cr._chunks) {
            const auto src_path  = src_cr.full_path(src_chunk.rel_path());
            const auto local_path = full_path(chunk_info::rel_path_from_hash(src_chunk.data_hash));
            std::filesystem::copy_file(src_path, local_path);
            add_file(src_chunk.offset, local_path);
        }
        _prepare_tx();
        _commit_tx();
    }

    void chunk_registry::add_buffer(const uint64_t offset, uint8_vector uncompressed, std::optional<uint8_vector> compressed)
    {
        const auto data_hash = crypto::blake2b::digest(uncompressed);
        const auto rel_path = fmt::format("chunk/{}.zstd.tmp", data_hash);
        const auto local_path = full_path(rel_path);
        if (!compressed)
            compressed.emplace(zstd::compress(uncompressed, 9));
        file::write(local_path, *compressed);
        _add(offset, local_path, std::move(uncompressed), compressed->size());
    }

    void chunk_registry::add_file(const uint64_t offset, const std::string &local_path)
    {
        const auto compressed = file::read(local_path);
        const auto uncompressed = zstd::decompress(compressed);
        _add(offset, local_path, uncompressed, compressed.size());
    }

    std::string chunk_registry::_add(const uint64_t offset, const std::string &local_path,
        const buffer uncompressed, const uint64_t compressed_size)
    {
        // TODO: add a fast path for data beyond earliest known invalid offset
        if (!_transaction) [[unlikely]]
            throw error("add can be executed only inside of a transaction!");
        auto [parsed_chunk, ex_ptr] = _parse(offset, uncompressed, compressed_size);
        const auto final_path = full_path(parsed_chunk.rel_path());
        if (!parsed_chunk.blocks.empty()) {
            if (!ex_ptr) {
                if (local_path != final_path)
                    std::filesystem::rename(local_path, final_path);
            } else {
                zstd::write(final_path, uncompressed.subbuf(0, parsed_chunk.block_data_size()));
            }
            _add(std::move(parsed_chunk));
        }
        if (ex_ptr)
            std::rethrow_exception(ex_ptr);
        return final_path;
    }

    [[nodiscard]] std::exception_ptr chunk_registry::accept_progress(const cardano::optional_point &start, const std::optional<progress_point> &target, const std::function<void()> &action)
    {
        return _accept_progress(start, target, true, action);
    }

    void chunk_registry::accept_anything_or_throw(const cardano::optional_point &start, const std::optional<progress_point> &target, const std::function<void()> &action)
    {
        if (const auto ex_ptr = _accept_progress(start, target, false, action); ex_ptr)
            std::rethrow_exception(ex_ptr);
    }

    void chunk_registry::truncate(const cardano::optional_point &new_tip)
    {
        if (const auto ex_ptr = _accept_progress(new_tip, new_tip, false, []{}); ex_ptr)
            std::rethrow_exception(ex_ptr);
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

    chunk_registry::const_iterator chunk_registry::latest_block_after_or_at_slot(const uint64_t slot) const
    {
        for (auto chunk_it = _find_chunk_by_slot(slot); chunk_it != _chunks.end(); ++chunk_it) {
            for (auto block_it = chunk_it->second.blocks.begin(); block_it != chunk_it->second.blocks.end(); ++block_it) {
                if (block_it->slot >= slot)
                    return { *this, _chunks, chunk_it, numeric_cast<size_t>(block_it - chunk_it->second.blocks.begin()) };
            }
        }
        return cend();
    }

    chunk_registry::const_iterator chunk_registry::latest_block_before_or_at_slot(const uint64_t slot) const
    {
        if (!_chunks.empty()) {
            auto chunk_it = _find_chunk_by_slot(slot);
            if (chunk_it == _chunks.end())
                --chunk_it;
            for (;;) {
                for (auto block_it = chunk_it->second.blocks.rbegin(); block_it != chunk_it->second.blocks.rend(); ++block_it) {
                    if (block_it->slot <= slot)
                        return { *this, _chunks, chunk_it, numeric_cast<size_t>(numeric_cast<ptrdiff_t>(chunk_it->second.blocks.size()) - (block_it - chunk_it->second.blocks.rbegin() + 1)) };;
                }
                if (chunk_it == _chunks.begin())
                    break;
                --chunk_it;
            }
        }
        return cend();
    }

    void chunk_registry::_node_export_chain(const std::filesystem::path &immutable_dir, const std::filesystem::path &volatile_dir, const int prio_base) const
    {
        // chunk registry may store the same Cardano Node chunk in multiple files, so need to combine them for the export
        struct merged_chunk {
            uint64_t first_slot = 0;
            uint64_t last_slot = 0;
            std::vector<std::string> files {};
            std::vector<const storage::block_info *> blocks {};
        };
        using merged_chunk_map = std::map<uint64_t, merged_chunk>;

        std::filesystem::remove_all(immutable_dir);
        std::filesystem::create_directories(immutable_dir);
        const auto done_bytes = std::make_shared<std::atomic_uint64_t>(0);
        const auto total_bytes = num_bytes();

        // split chunks into volatile and immutable ones
        std::vector<const storage::chunk_info *> volatile_chunks {};
        merged_chunk_map immutable_chunks {};
        const auto imm_tip = immutable_tip();
        for (const auto &[last_byte, chunk]: _chunks) {
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
            _sched.submit("decompress", prio_base, [this, done_bytes, total_bytes, chunk_id, m_chunk, immutable_dir, imm_tip] {
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
            std::vector<size_t> volatile_block_sizes {};
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
        try {
            if (normal && _transaction->target_slot() < chunk.last_slot)
                throw error(fmt::format("chunk's slot range {}:{} exceeds the target slot: {}",
                    chunk.first_slot, chunk.last_slot, _transaction->target_slot()));
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
            logger::debug("chunk_registry::_add: first_slot: {} last_slot: {} -> SUCCESS", make_slot(chunk.first_slot), make_slot(chunk.last_slot));
        } catch (...) {
            logger::debug("chunk_registry::_add: first_slot: {} last_slot: {} -> FAILURE", make_slot(chunk.first_slot), make_slot(chunk.last_slot));
        }
    }

    std::pair<storage::chunk_info, std::exception_ptr> chunk_registry::_parse(const uint64_t offset, const buffer &raw_data, const size_t compressed_size) const
    {
        std::exception_ptr ex_ptr{};
        std::optional<indexer::chunk_indexer_list> chunk_indexers{};
        if (_indexer)
            chunk_indexers = _indexer->make_chunk_indexers(offset);
        chunk_info chunk{.data_size=raw_data.size(), .compressed_size=compressed_size, .offset=offset};
        uint8_vector ok_data{};
        uint64_t prev_slot = 0;
        cbor::zero2::decoder dec{raw_data};
        while (!dec.done()) {
            try {
                auto &block_tuple = dec.read();
                const cardano::block_container blk_ptr{numeric_cast<uint64_t>(chunk.offset + block_tuple.data_begin() - raw_data.data()), block_tuple, _cardano_cfg};
                {
                    const auto &blk = *blk_ptr;
                    const auto slot = blk.slot();
                    if (slot < prev_slot) [[unlikely]]
                        throw error(fmt::format("chunk at {}: a block's slot {} is less than the slot of the prev block {}!", offset, slot, prev_slot));
                    prev_slot = slot;
                    static constexpr auto max_era = std::numeric_limits<uint8_t>::max();
                    if (blk.era() > max_era) [[unlikely]]
                        throw error(fmt::format("block at slot {} has era {} that is outside of the supported max limit of {}", slot, blk.era(), max_era));
                    static constexpr auto max_size = std::numeric_limits<uint32_t>::max();
                    if (blk_ptr.raw().size() > max_size) [[unlikely]]
                        throw error(fmt::format("block at slot {} has size {} that is outside of the supported max limit of {}", slot, blk_ptr.raw().size(), max_size));
                    if (!chunk.blocks.empty()) {
                        if (_validator && blk.prev_hash() != chunk.last_block_hash) [[unlikely]]
                            throw error(fmt::format("block at slot {} has an inconsistent prev_hash {}", blk.slot(), blk.prev_hash()));
                        const auto prev_chunk_id = cardano::slot::chunk_id(chunk.last_slot, _cardano_cfg);
                        const auto next_chunk_id = cardano::slot::chunk_id(slot, _cardano_cfg);
                        if (prev_chunk_id != next_chunk_id) [[unlikely]]
                            throw error(fmt::format("chunk at offset {} contains blocks from multiple chunks: {} and {}", offset, prev_chunk_id, next_chunk_id));
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
        crypto::blake2b::digest(chunk.data_hash, ok_data);
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

    epoch_info chunk_registry::_epoch(const uint64_t epoch) const
    {
        epoch_info::chunk_list chunks {};
        auto chunk_it = std::lower_bound(_chunks.begin(), _chunks.end(), epoch,
            [this](const auto &el, const auto &epoch) { return make_slot(el.second.first_slot).epoch() < epoch; });
        for (; chunk_it != _chunks.end() && make_slot(chunk_it->second.first_slot).epoch() == epoch; ++chunk_it) {
            chunks.emplace_back(&chunk_it->second);
        }
        return { std::move(chunks) };
    }

    void chunk_registry::_my_truncate(const cardano::optional_point &new_tip, const bool track_changes)
    {
        if (const auto max_end_offset = new_tip ? new_tip->end_offset : 0; max_end_offset < num_bytes()) {
            timer t { fmt::format("chunk_registry::_truncate to {}", new_tip), logger::level::info };
            auto chunk_it = _find_chunk_by_offset(max_end_offset);
            if (chunk_it->second.offset < max_end_offset) {
                auto block_it = _find_block_by_offset(chunk_it, max_end_offset);
                if (block_it == chunk_it->second.blocks.end())
                    throw error(fmt::format("internal error: no block covers offset {}", max_end_offset));
                // truncate chunk data and update its metadata
                if (track_changes)
                    _truncated_chunks.emplace(chunk_it->first, chunk_it->second);
                auto next_chunk_it = std::next(chunk_it);
                auto node = _chunks.extract(chunk_it);
                auto &chunk = node.mapped();
                chunk.blocks.resize(block_it - chunk.blocks.begin());
                chunk.num_blocks = chunk.blocks.size();
                chunk.data_size = chunk.blocks.back().end_offset() - chunk.offset;
                const auto old_path = full_path(chunk.rel_path());
                auto chunk_data = file::read_auto(old_path);
                chunk_data.resize(chunk.data_size);
                crypto::blake2b::digest(chunk.data_hash, chunk_data);
                const auto compressed = zstd::compress(chunk_data);
                file::write(full_path(chunk.rel_path()), compressed);
                chunk.compressed_size = compressed.size();
                chunk.last_slot = chunk.blocks.back().slot;
                chunk.last_block_hash = chunk.blocks.back().hash;
                node.key() = chunk.end_offset() - 1;
                chunk_it = _chunks.insert(next_chunk_it, std::move(node));
                ++chunk_it;
            }
            while (chunk_it != _chunks.end()) {
                if (track_changes)
                    _truncated_chunks.emplace(chunk_it->first, chunk_it->second);
                chunk_it = _chunks.erase(chunk_it);
            }
            // reconfigure time if truncating back into Byron era
            if (_chunks.empty() || _chunks.rbegin()->second.blocks.back().era < 2)
                _cardano_cfg.shelley_start_epoch({});
        }
    }

    void chunk_registry::_my_start_tx()
    {
        _tx_progress_max.clear();
        _tx_progress_parse.store(0, std::memory_order_relaxed);
        _notify_end_offset = num_bytes();
        _notify_next_epoch = _chunks.empty() ? 0 : make_slot(_chunks.rbegin()->second.first_slot).epoch();
        if (!_unmerged_chunks.empty()) {
            logger::warn("unmerged chunks weren't empty at the beginning of a tx - recovering from an error?");
            _unmerged_chunks.clear();
        }
    }

    uint64_t chunk_registry::_my_end_offset() const
    {
        return num_bytes();
    }

    void chunk_registry::_my_prepare_tx()
    {
        timer t { "chunk_registry::_prepare_tx" };
        if (!_unmerged_chunks.empty()) {
            logger::warn("{} unmerged chunks - ignoring them", _unmerged_chunks.size());
            if (!_chunks.empty())
                logger::trace("last merged chunk: {}", json::serialize(_chunks.rbegin()->second.to_json()));
            for (const auto &[last_byte_offset, uchunk]: _unmerged_chunks)
                logger::trace("unmerged chunk with last byte offset {}: {}", last_byte_offset, json::serialize(uchunk.to_json()));
            _unmerged_chunks.clear();
        }
        {
            mutex::unique_lock update_lk { _update_mutex };
            _notify_of_updates(update_lk, true);
        }
        // let the operations potentially scheduled in _on_epoch_merge calls to finish
        _sched.process(true);
        _save_state(_state_path_pre);
    }

    void chunk_registry::_my_rollback_tx()
    {
        for (auto chunk_it = _find_chunk_by_offset_no_throw(_transaction->start_offset()); chunk_it != _chunks.end(); ) {
            _file_remover.mark(full_path(chunk_it->second.rel_path()));
            chunk_it = _chunks.erase(chunk_it);
        }
        for (auto &&[last_offset, chunk]: _truncated_chunks) {
            const auto chunk_path = full_path(chunk.rel_path());
            _file_remover.unmark(chunk_path);
            const auto [it, created] = _chunks.try_emplace(chunk.offset + chunk.data_size - 1, std::move(chunk));
            if (!created)
                throw error(fmt::format("rollback failed: couldn't reinsert chunk {}", chunk_path));
        }
        _truncated_chunks.clear();
        _unmerged_chunks.clear();
    }

    void chunk_registry::_my_commit_tx()
    {
        if (!std::filesystem::exists(_state_path_pre))
            throw error(fmt::format("the prepared chunk_registry state file is missing: {}!", _state_path_pre));
        std::filesystem::rename(_state_path_pre, _state_path);
        for (const auto &[last_offset, chunk]: _truncated_chunks)
            _file_remover.mark(full_path(chunk.rel_path()));
        _truncated_chunks.clear();
        for (const auto &[last_byte_offset, chunk]: _chunks)
            _file_remover.unmark(full_path(chunk.rel_path()));
    }

    void chunk_registry::_require_better_candidate_chain()
    {
        const auto new_tip = tip();
        if (!new_tip || !(_transaction->start < new_tip))
            throw error(fmt::format("candidate chain is not better: proposed tip: {} intersection: {}", new_tip, _transaction->start));
        if (!_truncated_chunks.empty()) {
            // slot window for the chain density calculation
            auto window_last_slot = cardano::density_default_window;
            size_t fork_prev_height = 0;

            auto first_it = const_iterator::cbegin(*this, _truncated_chunks);
            const auto tr_cend = const_iterator::cend(*this, _truncated_chunks);
            std::optional<storage::block_info> last_common_block {};
            if (_transaction->start_offset() > 0) {
                last_common_block = find_block_by_offset(_transaction->start_offset() - 1);
                window_last_slot += last_common_block->slot;
                while (first_it != tr_cend && *first_it != *last_common_block)
                    ++first_it;
            }
            if (first_it != const_iterator::cend(*this, _truncated_chunks)) {
                auto last_it = first_it;
                while (last_it != tr_cend && last_it->slot <= window_last_slot) {
                    ++last_it;
                }
                fork_prev_height = const_iterator::block_distance(last_it, first_it).height();
            }

            // some candidate blocks may have not passed the delayed steps of the validation
            const auto new_first_it = find_by_offset(new_tip->end_offset - 1);
            const auto last_valid_slot = std::min(window_last_slot, new_tip->slot);
            auto new_last_it = new_first_it;
            const auto new_cend = cend();
            while (new_last_it != new_cend && new_last_it->slot <= last_valid_slot) {
                ++new_last_it;
            }
            const auto fork_new_height = const_iterator::block_distance(new_last_it, new_first_it).height();

            if (fork_prev_height >= fork_new_height)
                throw error(fmt::format("candidate chain at byte {} is not better than the original: candidate block count {} vs {}",
                    _transaction->start_offset(), fork_new_height, fork_prev_height));
        }
    }

    // does not report an error if some progress is made
    [[nodiscard]] std::exception_ptr chunk_registry::_accept_progress(const cardano::optional_point &start, const std::optional<progress_point> &target,
            const bool aim_progress, const std::function<void()> &action) {
        _start_tx(start, target);
        const auto act_err = logger::run_log_errors([&] {
            action();
        });
        bool commit_ok = false;
        std::exception_ptr commit_err = nullptr;
        if (!act_err || aim_progress) {
            commit_err = logger::run_log_errors([&]{
                _prepare_tx();
                if (aim_progress)
                    _require_better_candidate_chain();
                _commit_tx();
            });
            commit_ok = !commit_err;
        }
        if (!commit_ok) {
            logger::debug("rollback triggers: action error: {} commit error: {}", !!act_err, !!commit_err);
            logger::run_log_errors([&] {
                _rollback_tx();
            });
            // ensure there are no run-away tasks
            logger::run_log_errors([&] {
                _sched.process(true);
            });
        }
        return act_err ? act_err : commit_err;
    }

    void chunk_registry::_start_tx(cardano::optional_point start, const std::optional<progress_point> &target)
    {
        timer t { "chunk_registry::start_tx", logger::level::debug };
        if (_transaction)
            throw error("nested transactions are not allowed!");
        if (target < start)
            throw error(fmt::format("the target slot {} cannot be smaller than the start chain {}", target, start));
        if (start) {
            // checks that the requested start point is known
            const auto &block = get_block_info(cardano::point2 { start->slot, start->hash });
            // ensure we use the internally verified data about the start point
            start->height = block.height;
            start->end_offset = block.end_offset();
        }
        _transaction = active_transaction { start, target };
        // must happen before a potential truncate below
        if (!_truncated_chunks.empty()) {
            logger::warn("truncated chunks weren't empty at the beginning of a tx - recovering from an error?");
            _truncated_chunks.clear();
        }
        _do_truncate(_transaction->start, true);
        _my_start_tx();
        for (const auto *p: _processors) {
            if (p->start_tx)
                p->start_tx();
        }
    }

    void chunk_registry::_prepare_tx()
    {
        timer t { "chunk_registry::prepare_tx", logger::level::debug };
        if (!_transaction)
            throw error("prepare_tx can be executed only inside of a transaction!");
        _my_prepare_tx();
        for (const auto *p: _processors) {
            if (p->prepare_tx)
                p->prepare_tx();
        }
        _do_truncate(tip(), false);
        _transaction->prepared = true;
    }

    void chunk_registry::_rollback_tx()
    {
        if (!_transaction)
            throw error("rollback_tx can be executed only inside of a transaction!");
        _my_rollback_tx();
        for (const auto *p: _processors) {
            if (p->rollback_tx)
                p->rollback_tx();
        }
        _transaction.reset();
    }

    void chunk_registry::_commit_tx()
    {
        timer t { "chunk_registry::commit_tx", logger::level::debug };
        if (!_transaction)
            throw error("commit_tx can be executed only inside of a transaction!");
        if (!_transaction->prepared)
            throw error("commit_tx can only be executed after a successful prepare_tx!");
        _my_commit_tx();
        for (const auto *p: _processors) {
            if (p->commit_tx)
                p->commit_tx();
        }
        _transaction.reset();
    }

    void chunk_registry::_save_state(const std::string &path)
    {
        // the caller is responsible to hold a lock protecting access to the _chunks!
        zpp::save(path, _chunks);
    }

    void chunk_registry::_do_truncate(const cardano::optional_point &new_tip, const bool track_changes)
    {
        if (!_transaction)
            throw error("truncate can be executed only inside of a transaction!");
        if (new_tip < _transaction->start)
            throw error("truncation must happen only within the target transaction slot range!");
        logger::debug("truncate the local chain to {}", new_tip);
        _my_truncate(new_tip, track_changes);
        for (const auto *p: _processors) {
            if (p->truncate)
                p->truncate(new_tip, track_changes);
        }
    }

    void chunk_registry::_notify_of_updates(mutex::unique_lock &update_lk, bool force)
    {
        if (!update_lk)
            throw error("update_mutex must be locked when _notify_of_updates is called!");
        const auto max_epoch = make_slot(max_slot()).epoch();
        const auto end_offset = num_bytes();
        if (!force && _transaction->target && _transaction->target->slot == max_slot())
            force = true;
        while (end_offset > _notify_end_offset && (_notify_next_epoch < max_epoch || (force && _notify_next_epoch == max_epoch))) {
            // in unit-tests chunks may have non-continuous epochs
            if (_has_epoch(_notify_next_epoch, update_lk)) {
                const auto einfo = _epoch(_notify_next_epoch);
                epoch_info::chunk_list filtered_chunks {};
                for (const auto *chunk: einfo.chunks()) {
                    if (chunk->offset >= _notify_end_offset)
                        filtered_chunks.emplace_back(chunk);
                }
                _notify_end_offset = einfo.end_offset();
                if (!filtered_chunks.empty()) {
                    const epoch_info update_info { std::move(filtered_chunks) };
                    for (const auto *p: _processors) {
                        if (p->on_epoch_update)
                            p->on_epoch_update(_notify_next_epoch, update_info);
                    }
                }
            }
            ++_notify_next_epoch;
        }
    }

    chunk_registry::chunk_map::iterator chunk_registry::_find_chunk_by_offset(const uint64_t offset)
    {
        const auto it = _chunks.lower_bound(offset);
        if (it == _chunks.end())
            throw error(fmt::format("no chunk matches offset: {}!", offset));
        return it;
    }

    chunk_registry::chunk_map::const_iterator chunk_registry::_find_chunk_by_offset_no_throw(const uint64_t offset) const
    {
        return _chunks.lower_bound(offset);
    }

    chunk_registry::chunk_map::const_iterator chunk_registry::_find_chunk_by_offset(const uint64_t offset) const
    {
        const auto it = _find_chunk_by_offset_no_throw(offset);
        if (it == _chunks.end())
            throw error(fmt::format("no chunk matches offset: {}!", offset));
        return it;
    }

    storage::block_list::const_iterator chunk_registry::_find_block_by_offset(const chunk_map::const_iterator chunk_it, const uint64_t offset) const
    {
        if (chunk_it == _chunks.end())
            throw error(fmt::format("internal error: a non-empty chunk_iterator is expected!"));
        const auto &blocks = chunk_it->second.blocks;
        const auto block_it = std::lower_bound(blocks.begin(), blocks.end(), offset,
            [](const auto &b, const auto offset) { return b.end_offset() - 1 < offset; });
        return block_it;
    }

    // can return the closest succeeding chunk if no chunk includes the block
    chunk_registry::chunk_map::const_iterator chunk_registry::_find_chunk_by_slot(const uint64_t slot) const
    {
        const auto chunk_it = std::lower_bound(_chunks.begin(), _chunks.end(), slot,
            [](const auto &c, const auto &slot) { return c.second.last_slot < slot; });
        return chunk_it;
    }

    // can return the closest succeeding block if there is no block at that slot
    storage::block_list::const_iterator chunk_registry::_find_block_by_slot(const chunk_map::const_iterator chunk_it, const uint64_t slot) const
    {
        if (chunk_it == _chunks.end()) [[unlikely]]
            throw error("internal error: a non-empty chunk_iterator is expected!");
        return std::lower_bound(chunk_it->second.blocks.begin(), chunk_it->second.blocks.end(), slot,
            [](const auto &b, const auto &slot) { return b.slot < slot; });
    }

    bool chunk_registry::_has_epoch(const uint64_t epoch, mutex::unique_lock &update_lk) const
    {
        if (!update_lk)
            throw error("internal error: update lock must be held at the call to _has_epoch");
        const auto chunk_it = std::lower_bound(_chunks.begin(), _chunks.end(), epoch,
            [this](const auto &el, const auto &epoch) { return make_slot(el.second.first_slot).epoch() < epoch; });
        return chunk_it != _chunks.end() && make_slot(chunk_it->second.first_slot).epoch() == epoch;
    }
}
