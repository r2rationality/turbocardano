#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <dt/cardano/common/config.hpp>
#include <dt/cardano.hpp>
#include <dt/file.hpp>
#include <dt/file-remover.hpp>
#include <dt/indexer.hpp>
#include <dt/json.hpp>
#include <dt/progress.hpp>
#include <dt/scheduler.hpp>
#include <dt/storage/chunk-info.hpp>
#include <dt/timer.hpp>
#include <dt/validator.hpp>
#include <dt/zpp.hpp>

namespace daedalus_turbo {

    namespace indexer {
        struct incremental;
    }

    typedef uint64_t chunk_offset_t;

    struct epoch_info {
        using chunk_list = storage::chunk_cptr_list;

        epoch_info(chunk_list &&chunks): _chunks { std::move(chunks) }
        {
            if (_chunks.empty())
                throw error("chunk list cannot be empty!");
        }

        const chunk_list &chunks() const
        {
            return _chunks;
        }

        const cardano::block_hash &prev_block_hash() const
        {
            return _chunks.front()->prev_block_hash;
        }

        [[nodiscard]] const cardano::block_hash &last_block_hash() const
        {
            return _chunks.back()->last_block_hash;
        }

        [[nodiscard]] uint64_t first_slot() const
        {
            return _chunks.front()->first_slot;
        }

        [[nodiscard]] uint64_t last_slot() const
        {
            return _chunks.back()->last_slot;
        }

        [[nodiscard]] uint64_t start_offset() const
        {
            return _chunks.front()->offset;
        }

        [[nodiscard]] uint64_t end_offset() const
        {
            return _chunks.back()->end_offset();
        }

        [[nodiscard]] uint64_t size() const
        {
            return end_offset() - start_offset();
        }

        [[nodiscard]] uint64_t era() const
        {
            const auto first_era = _chunks.front()->era();
            const auto last_era = _chunks.back()->era();
            if (first_era == last_era || (first_era == 0 && last_era == 1)) [[likely]]
                return last_era;
            throw error(fmt::format("epoch has blocks from multiple eras {} and {}", first_era, last_era));
        }

        [[nodiscard]] uint64_t compressed_size() const
        {
            uint64_t sz = 0;
            for (const auto *chunk: _chunks)
                sz += chunk->compressed_size;
            return sz;
        }
    private:
        chunk_list _chunks;
    };
    using epoch_map = map<size_t, epoch_info>;

    struct progress_point {
        uint64_t slot = 0; // required to be correct
        uint64_t end_offset = 0; // can be zero; a non-zero value is used for more accurate process calculations

        progress_point(const uint64_t slot_)
            : slot { slot_ }
        {
        }

        progress_point(const uint64_t slot_, const uint64_t end_offset_)
            : slot { slot_ }, end_offset { end_offset_ }
        {
        }

        progress_point(const cardano::point &p)
            : slot { p.slot }, end_offset { p.end_offset }
        {
        }

        progress_point() =delete;
        progress_point(const progress_point &o) =default;
        progress_point &operator=(const progress_point &o) =default;

        bool operator<(const progress_point &o) const
        {
            return slot < o.slot;
        }
    };
    using optional_progress_point = std::optional<progress_point>;

    inline bool operator<(const optional_progress_point &a, const cardano::optional_point &b)
    {
        if (a.has_value() && b.has_value())
            return a.value().slot < b.value().slot;
        if (a.has_value() != b.has_value())
            return a.has_value() < b.has_value();
        return false;
    }

    inline bool operator<(const cardano::optional_point &a, const optional_progress_point &b)
    {
        if (a.has_value() && b.has_value())
            return a.value().slot < b.value().slot;
        if (a.has_value() != b.has_value())
            return a.has_value() < b.has_value();
        return false;
    }

    inline bool operator<(const cardano::optional_slot &a, const optional_progress_point &b)
    {
        if (a.has_value() && b.has_value())
            return a.value() < b.value().slot;
        if (a.has_value() != b.has_value())
            return a.has_value() < b.has_value();
        return false;
    }

    struct chunk_processor {
        std::function<uint64_t()> end_offset {};
        std::function<void()> start_tx {};
        std::function<void()> prepare_tx {};
        std::function<void()> rollback_tx {};
        std::function<void()> commit_tx {};
        std::function<void(const cardano::optional_point &, bool)> truncate {};
        std::function<void(const cardano::block_base &)> on_block_validate {};
        std::function<void(const storage::chunk_info &)> on_chunk_add {};
        std::function<void(uint64_t, const epoch_info &)> on_epoch_update {};
        std::function<void(std::string_view, uint64_t, uint64_t)> on_progress {};
    };

    struct chunk_registry {
        enum class mode { store, index, validate };

        // Shall be a multiple of an SSD's sector size and larger than Cardano's largest block (including Byron boundary ones too!)
        using chunk_info = storage::chunk_info;
        using chunk_map = std::map<uint64_t, chunk_info>;
        using chunk_list = std::vector<chunk_info>;
        using chunk_reverse_iterator = chunk_map::const_reverse_iterator;

        struct const_iterator {
            const_iterator() =delete;

            const_iterator(const const_iterator &o) noexcept:
                _cr { o._cr },
                _chunk_it { o._chunk_it },
                _block_no { o._block_no }
            {
            }

            const_iterator &operator=(const const_iterator &o)
            {
                if (&_cr != &o._cr) [[unlikely]]
                    throw error(fmt::format("an attempt to assing an iterator across different instances of chunk_registry"));
                _chunk_it = o._chunk_it;
                _block_no = o._block_no;
                _chunk_cache.reset();
                return *this;
            }

            bool operator==(const const_iterator &o) const
            {
                return static_cast<int>(&_cr == &o._cr) & static_cast<int>(_chunk_it == o._chunk_it) & static_cast<int>(_block_no == o._block_no);
            }

            const storage::block_info &operator*() const
            {
                return _chunk_it->second.blocks.at(_block_no);
            }

            const storage::block_info *operator->() const
            {
                return &_chunk_it->second.blocks.at(_block_no);
            }

            cardano::parsed_header header() const;
            uint8_vector block_data() const;
            std::pair<uint8_vector, const_iterator> chunk_remaining_data(const_iterator last_it) const;

            const_iterator &operator++()
            {
                if (_chunk_it != _cr._chunks.end()) {
                    ++_block_no;
                    if (_block_no == _chunk_it->second.blocks.size()) [[likely]] {
                        ++_chunk_it;
                        _block_no = 0;
                    }
                }
                return *this;
            }
        private:
            friend chunk_registry;

            struct chunk_cache {
                std::string full_path;
                write_vector data;
            };

            const chunk_registry &_cr;
            chunk_map::const_iterator _chunk_it;
            size_t _block_no = 0;
            mutable std::optional<chunk_cache> _chunk_cache {};

            const_iterator(const chunk_registry &cr, const chunk_map::const_iterator chunk_it, const size_t block_no) noexcept:
                _cr { cr },
                _chunk_it { chunk_it },
                _block_no { block_no }
            {
            }

            buffer _prep_chunk_cache() const;
        };

        struct active_transaction {
            cardano::optional_point start {};
            std::optional<progress_point> target {};
            bool prepared = false;

            uint64_t start_offset() const
            {
                if (start.has_value()) [[likely]] {
                    if (start->end_offset) [[likely]]
                        return start->end_offset;
                    throw error("misconfigured transaction start point: no offset defined!");
                }
                return 0;
            }

            uint64_t start_slot() const
            {
                if (start.has_value()) [[likely]]
                    return start->slot;
                return 0;
            }

            uint64_t target_slot() const
            {
                if (target.has_value()) [[likely]]
                    return target->slot;
                return start_slot();
            }

            uint64_t target_offset() const
            {
                if (target.has_value()) [[likely]] {
                    if (target->end_offset) [[likely]]
                        return target->end_offset;
                    throw error("request for a transaction's target offset when the transaction doesn't have it!");
                }
                return start_offset();
            }
        };

        using file_set = std::set<std::string>;
        using block_processor = std::function<void(const cardano::block_base &)>;

        static std::filesystem::path init_db_dir(const std::string &db_dir)
        {
            std::filesystem::create_directories(db_dir);
            return std::filesystem::canonical(db_dir);
        }

        explicit chunk_registry(const std::string &data_dir, mode mode=mode::validate,
            cardano::config ccfg=cardano::config::get(), scheduler &sched=scheduler::get(), file_remover &fr=file_remover::get(),
            bool auto_maintenance=true);
        ~chunk_registry();

        // Interoperability

        void register_processor(const chunk_processor &p);
        void remove_processor(const chunk_processor &p);
        void report_progress(const std::string_view name, const progress_point &tip) const;

        std::optional<active_transaction> tx() const
        {
            return _transaction;
        }

        void validation_failure_handler(const std::function<void(uint64_t)> &);
        const indexer::incremental &indexer() const;
        const validator::incremental &validator() const;

        const cardano::config &config() const
        {
            return _cardano_cfg;
        }

        scheduler &sched() const
        {
            return _sched;
        }

        file_remover &remover() const
        {
            return _file_remover;
        }

        // data accessors

        const_iterator cbegin() const
        {
            return { *this, _chunks.begin(), 0 };
        }

        const_iterator cend() const
        {
            return { *this, _chunks.end(), 0 };
        }

        const chunk_map &chunks() const
        {
            return _chunks;
        }

        epoch_map epochs() const
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

        bool has_epoch(uint64_t epoch) const
        {
            mutex::unique_lock lk { _update_mutex };
            return _has_epoch(epoch, lk);
        }

        uint64_t block_height(const uint64_t slot, const buffer hash) const
        {
            uint64_t h = 0;
            for (const auto &[last_byte, chunk]: _chunks) {
                if (chunk.first_slot > slot)
                    break;
                for (const auto &b: chunk.blocks) {
                    if (b.slot <= slot) {
                        if (b.era)
                            ++h;
                        if (b.slot == slot && b.hash == hash)
                            return h;
                    } else {
                        break;
                    };
                }
            }
            throw error(fmt::format("unknown block slot: {} hash: {}", slot, hash));
        }

        cardano::slot make_slot(uint64_t slot_) const
        {
            return { slot_, _cardano_cfg };
        }

        uint64_t num_bytes() const
        {
            if (!_chunks.empty()) [[likely]]
                return _chunks.rbegin()->second.end_offset();
            return 0;
        }

        uint64_t num_compressed_bytes() const
        {
            if (!_chunks.empty()) [[likely]]
                return std::accumulate(_chunks.begin(), _chunks.end(), 0ULL,
                    [](auto sum, const auto &chunk) { return sum + chunk.second.compressed_size; });
            return 0;
        }

        size_t num_chunks() const
        {
            return _chunks.size();
        }

        size_t num_blocks() const
        {
            return std::accumulate(_chunks.begin(), _chunks.end(), static_cast<size_t>(0),
                [](auto sum, const auto &val) { return sum + val.second.blocks.size(); });
        }

        const storage::chunk_info *find_chunk_by_offset_no_throw(const uint64_t offset) const
        {
            if (const auto chunk_it = _find_chunk_by_offset_no_throw(offset); chunk_it != _chunks.end()) [[likely]]
                return &chunk_it->second;
            return nullptr;
        }

        std::optional<storage::block_info> find_block_by_offset_no_throw(const uint64_t offset) const
        {
            if (const auto chunk_it = _find_chunk_by_offset(offset); chunk_it != _chunks.end()) {
                if (const auto block_it = _find_block_by_offset(chunk_it, offset); block_it != chunk_it->second.blocks.end()) {
                    if (offset >= block_it->offset && offset < block_it->offset + block_it->size)
                        return *block_it;
                    throw error("internal error: block metadata does not match the transaction!");
                }
            }
            return {};
        }

        storage::block_info find_block_by_offset(const uint64_t offset) const
        {
            if (const auto block = find_block_by_offset_no_throw(offset))
                return *block;
            throw error(fmt::format("unknown offset: {}!", offset));
        }

        const storage::block_info &find_block_by_slot(const uint64_t slot) const
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

        const storage::chunk_info &find_chunk_by_slot(const uint64_t slot) const
        {
            const auto chunk_it = _find_chunk_by_slot(slot);
            if (chunk_it == _chunks.end()) [[unlikely]]
                throw error(fmt::format("internal error: no block registered at a slot: {}!", slot));
            return chunk_it->second;
        }

        std::optional<storage::block_info> latest_block_after_or_at_slot(uint64_t slot) const;
        std::optional<storage::block_info> latest_block_before_or_at_slot(uint64_t slot) const;

        const_iterator find_block(const cardano::point2 &p) const
        {
            if (auto chunk_it = _find_chunk_by_slot(p.slot); chunk_it != _chunks.end()) [[likely]] {
                if (auto block_it = _find_block_by_slot(chunk_it, p.slot); block_it != chunk_it->second.blocks.end()) [[likely]] {
                    if (block_it->slot == p.slot) {
                        for (;;) {
                            if (block_it->hash == p.hash) [[likely]]
                                return { *this, chunk_it, numeric_cast<size_t>(block_it - chunk_it->second.blocks.begin()) };
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

        std::optional<storage::block_info> find_block_by_slot_no_throw(const uint64_t slot, const cardano::block_hash &hash) const
        {
            if (const auto it = find_block(cardano::point2 { slot, hash }); it != cend()) [[likely]] {
                return { *it };
            }
            return {};
        }

        storage::block_info find_block_by_slot(const uint64_t slot, const cardano::block_hash &hash) const
        {
            if (const auto block = find_block_by_slot_no_throw(slot, hash); block) [[likely]]
                return *block;
            throw error(fmt::format("internal error: no such block: {} {}!", slot, hash));
        }

        uint64_t find_epoch(const uint64_t offset) const
        {
            mutex::scoped_lock lk { _update_mutex };
            return make_slot(find_offset(offset).first_slot).epoch();
        }

        const chunk_info &find_offset(uint64_t offset) const
        {
            return _find_chunk_by_offset(offset)->second;
        }

        const chunk_info &find_last_block_hash(const buffer &last_block_hash) const
        {
            const auto it = std::find_if(_chunks.begin(), _chunks.end(),
                                         [&](const auto &el) { return el.second.last_block_hash == last_block_hash; });
            if (it == _chunks.end())
                throw error(fmt::format("there is no chunk with its last block hash {}", last_block_hash));
            return it->second;
        }

        chunk_map::const_iterator find_slot_it(const uint64_t slot) const
        {
            const auto chunk_it = _find_chunk_by_slot(slot);
            if (chunk_it == _chunks.end())
                throw error(fmt::format("there is no chunk with a block at slot {}", slot));
            return chunk_it;
        }

        chunk_map::const_iterator find_offset_it(uint64_t offset) const
        {
            return _find_chunk_by_offset(offset);
        }

        chunk_map::const_iterator find_data_hash_it(const buffer &data_hash) const
        {
            return std::find_if(_chunks.begin(), _chunks.end(),
                [&](const auto &el) { return el.second.data_hash == data_hash; });
        }

        size_t count_blocks(const std::optional<cardano::point> &start_point, const uint64_t last_slot)
        {
            // cannot allow updates because that will invalidate the iterators
            mutex::scoped_lock lk { _update_mutex };
            size_t num_data_blocks = 0;
            auto chunk_it = start_point ? _find_chunk_by_slot(start_point->slot) : _chunks.begin();
            bool seen_start_hash = false;
            for (; chunk_it != _chunks.end() && chunk_it->second.first_slot <= last_slot; ++chunk_it) {
                for (const auto &block: chunk_it->second.blocks) {
                    if (block.slot > last_slot)
                        break;
                    if (start_point) {
                        if (block.slot < start_point->slot)
                            continue;
                        // There is a miniscule probability that multiple blocks are generated in the same slot.
                        // In this case, we must start counting only after seeing the right block hash
                        if (block.slot == start_point->slot) {
                            if (!seen_start_hash && block.hash != start_point->hash)
                                continue;
                            seen_start_hash = true;
                        }
                    }
                    if (block.era > 0)
                        ++num_data_blocks;
                }
            }
            return num_data_blocks;
        }

        size_t count_blocks_in_window(const std::optional<cardano::point> &start_point={}, const uint64_t window_size=cardano::density_default_window)
        {
            uint64_t last_slot = window_size;
            if (start_point)
                last_slot += static_cast<uint64_t>(start_point->slot);
            return count_blocks(start_point, last_slot);
        }

        cardano::amount unspent_reward(const cardano::stake_ident &id) const;
        cardano::tail_relative_stake_map tail_relative_stake() const;


        cardano::optional_point tip() const;
        cardano::optional_point core_tip() const;
        cardano::optional_point immutable_tip() const;

        std::optional<chunk_info> last_chunk() const
        {
            if (!_chunks.empty()) [[likely]]
                return _chunks.rbegin()->second;
            return {};
        }

        std::optional<storage::block_info> last_valid_block() const
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

        std::optional<storage::block_info> last_block() const
        {
            if (!_chunks.empty()) [[likely]] {
                return _chunks.rbegin()->second.blocks.back();
            }
            return {};
        }

        uint64_t max_slot() const
        {
            if (!_chunks.empty()) [[likely]]
                return _chunks.rbegin()->second.last_slot;
            return 0;
        }

        uint64_t valid_end_offset() const
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

        uint64_t max_end_offset() const
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

        const std::filesystem::path &data_dir() const
        {
            return _data_dir;
        }

        std::string rel_path(const std::filesystem::path &full_path) const
        {
            auto canon_path = std::filesystem::weakly_canonical(full_path);
            auto [diffBegin, diffEnd] = std::mismatch(_db_dir.begin(), _db_dir.end(), canon_path.begin());
            if (diffBegin != _db_dir.end())
                throw error(fmt::format("the supplied path '{}' is not inside the host directory '{}'", canon_path.string(), _db_dir.string()));
            return std::filesystem::relative(canon_path, _db_dir).string();
        }

        std::string full_path(const std::filesystem::path &rel_path) const
        {
            auto canon_path = std::filesystem::weakly_canonical(_db_dir / rel_path);
            auto [diffBegin, diffEnd] = std::mismatch(_db_dir.begin(), _db_dir.end(), canon_path.begin());
            if (diffBegin != _db_dir.end())
                throw error(fmt::format("the supplied path '{}' does not resolve into the host directory '{}'", canon_path.string(), _db_dir.string()));
            std::filesystem::create_directories(canon_path.parent_path());
            return canon_path.string();
        }

        uint64_t read_holding_chunk(uint8_vector &chunk_data, const uint64_t offset) const
        {
            if (offset >= num_bytes())
                throw error(fmt::format("the requested offset {} is larger than the maximum one: {}", offset, num_bytes()));
            const auto &chunk = find_offset(offset);
            if (offset >= chunk.offset + chunk.data_size)
                throw error("the requested chunk segment is too small to parse it");
            file::read_auto(full_path(chunk.rel_path()), chunk_data);
            return chunk.offset;
        }

        cbor::zero2::parsed_value read_from_chunk_buffer(const uint64_t value_offset, const buffer &chunk_data, const uint64_t chunk_offset) const
        {
            if (value_offset < chunk_offset) [[unlikely]]
                throw error("the requested value offset is outside of the chunk's data range!");
            if (value_offset >= chunk_offset + chunk_data.size()) [[unlikely]]
                throw error("the requested chunk segment is too small to parse it");
            const size_t read_offset = value_offset - chunk_offset;
            const size_t read_size = chunk_data.size() - read_offset;
            return cbor::zero2::parse(chunk_data.subbuf(read_offset, read_size));
        }

        cbor::zero2::parsed_value read(const uint64_t offset) const
        {
            const auto chunk_offset = read_holding_chunk(_read_buffer, offset);;
            return read_from_chunk_buffer(offset, _read_buffer, chunk_offset);
        }

        // state modifying methods

        void import(const chunk_registry &src_cr)
        {
            uint8_vector raw_data {}, compressed_data {};
            _start_tx(tip(), src_cr.tip());
            for (const auto &[last_byte_offset, src_chunk]: src_cr.chunks()) {
                const auto src_path  = src_cr.full_path(src_chunk.rel_path());
                const auto local_path = full_path(chunk_info::rel_path_from_hash(src_chunk.data_hash));
                std::filesystem::copy_file(src_path, local_path);
                add(src_chunk.offset, local_path);
            }
            _prepare_tx();
            _commit_tx();
        }

        std::string add_compressed(const uint64_t offset, uint8_vector compressed, uint8_vector uncompressed)
        {
            const auto data_hash = blake2b<blake2b_256_hash>(uncompressed);
            const auto rel_path = fmt::format("chunk/{}.zstd.tmp", data_hash);
            const auto local_path = full_path(rel_path);
            file::write(local_path, compressed);
            return add(offset, local_path, std::move(uncompressed));
        }

        std::string add(const uint64_t offset, const std::string &local_path, std::optional<uint8_vector> uncompressed={})
        {
            // TODO: add a fast path for data beyond earliest known invalid offset
            if (!_transaction)
                throw error("add can be executed only inside of a transaction!");
            const auto compressed = file::read(local_path);
            if (!uncompressed)
                uncompressed.emplace(zstd::decompress(compressed));
            auto [parsed_chunk, ex_ptr] = _parse(offset, *uncompressed, compressed.size());
            const auto final_path = full_path(parsed_chunk.rel_path());
            if (!parsed_chunk.blocks.empty()) {
                if (!ex_ptr) {
                    if (local_path != final_path)
                        std::filesystem::rename(local_path, final_path);
                } else {
                    zstd::write(final_path, static_cast<buffer>(*uncompressed).subbuf(0, parsed_chunk.block_data_size()));
                }
                _add(std::move(parsed_chunk));
            }
            if (ex_ptr)
                std::rethrow_exception(ex_ptr);
            return final_path;
        }

        [[nodiscard]] std::exception_ptr accept_progress(const cardano::optional_point &start, const std::optional<progress_point> &target, const std::function<void()> &action)
        {
            return _accept_progress(start, target, true, action);
        }

        void accept_anything_or_throw(const cardano::optional_point &start, const std::optional<progress_point> &target, const std::function<void()> &action)
        {
            if (const auto ex_ptr = _accept_progress(start, target, false, action); ex_ptr)
                std::rethrow_exception(ex_ptr);
        }

        void accept_progress_or_throw(const cardano::optional_point &start, const std::optional<progress_point> &target, const std::function<void()> &action)
        {
            if (const auto ex_ptr = accept_progress(start, target, action); ex_ptr)
                std::rethrow_exception(ex_ptr);
        }

        void truncate(const cardano::optional_point &new_tip)
        {
            if (const auto ex_ptr = _accept_progress(new_tip, new_tip, false, []{}); ex_ptr)
                std::rethrow_exception(ex_ptr);
        }

        // data export

        cardano::optional_slot can_export() const;
        void node_export(const std::filesystem::path &node_dir, const cardano::point &tip, bool ledger_only=false) const;
        std::string node_export_ledger(const std::filesystem::path &ledger_dir, const cardano::optional_point &imm_tip, int prio=1000) const;
    private:
        const std::filesystem::path _data_dir;
        const std::filesystem::path _db_dir;
        const cardano::config _cardano_cfg;
        scheduler &_sched;
        file_remover &_file_remover;
        set<const chunk_processor *> _processors {}; // initialize before indexer and validator who call register_processor/remove_processor
        std::unique_ptr<indexer::incremental> _indexer {};
        std::unique_ptr<validator::incremental> _validator {};
        std::optional<active_transaction> _transaction {};
        mutable mutex::unique_lock::mutex_type _tx_progress_mutex alignas(mutex::alignment) {};
        mutable map<std::string, uint64_t> _tx_progress_max {};
        mutable std::atomic_size_t _tx_progress_parse { 0 };
        const std::string _state_path;
        const std::string _state_path_pre;
        mutable mutex::unique_lock::mutex_type _update_mutex alignas(mutex::alignment) {};
        chunk_map _chunks {};
        // Active transaction data
        chunk_map _unmerged_chunks {};
        uint64_t _notify_end_offset = 0;
        uint64_t _notify_next_epoch = 0;
        vector<chunk_info> _truncated_chunks {};
        static thread_local uint8_vector _read_buffer;

        void _maintenance();
        void _node_export_chain(const std::filesystem::path &immutable_dir, const std::filesystem::path &volatile_dir, int prio_base=100) const;
        std::pair<chunk_info, std::exception_ptr> _parse(const uint64_t offset, const buffer &raw_data, const size_t compressed_size) const;

        epoch_info _epoch(const uint64_t epoch) const
        {
            epoch_info::chunk_list chunks {};
            auto chunk_it = std::lower_bound(_chunks.begin(), _chunks.end(), epoch,
                [this](const auto &el, const auto &epoch) { return make_slot(el.second.first_slot).epoch() < epoch; });
            for (; chunk_it != _chunks.end() && make_slot(chunk_it->second.first_slot).epoch() == epoch; ++chunk_it) {
                chunks.emplace_back(&chunk_it->second);
            }
            return { std::move(chunks) };
        }

        void _my_truncate(const cardano::optional_point &new_tip, const bool track_changes)
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
                        _truncated_chunks.emplace_back(chunk_it->second);
                    auto next_chunk_it = std::next(chunk_it);
                    auto node = _chunks.extract(chunk_it);
                    auto &chunk = node.mapped();
                    chunk.blocks.resize(block_it - chunk.blocks.begin());
                    chunk.num_blocks = chunk.blocks.size();
                    chunk.data_size = chunk.blocks.back().end_offset() - chunk.offset;
                    const auto old_path = full_path(chunk.rel_path());
                    auto chunk_data = file::read_auto(old_path);
                    chunk_data.resize(chunk.data_size);
                    blake2b(chunk.data_hash, chunk_data);
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
                        _truncated_chunks.emplace_back(chunk_it->second);
                    chunk_it = _chunks.erase(chunk_it);
                }
                // reconfigure time if truncating back into Byron era
                if (_chunks.empty() || _chunks.rbegin()->second.blocks.back().era < 2)
                    _cardano_cfg.shelley_start_epoch({});
            }
        }

        void _my_start_tx()
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

        uint64_t _my_end_offset() const
        {
            return num_bytes();
        }

        void _my_prepare_tx()
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

        void _my_rollback_tx()
        {
            for (auto chunk_it = _find_chunk_by_offset_no_throw(_transaction->start_offset()); chunk_it != _chunks.end(); ) {
                _file_remover.mark(full_path(chunk_it->second.rel_path()));
                chunk_it = _chunks.erase(chunk_it);
            }
            for (auto &&chunk: _truncated_chunks) {
                const auto chunk_path = full_path(chunk.rel_path());
                _file_remover.unmark(chunk_path);
                const auto [it, created] = _chunks.try_emplace(chunk.offset + chunk.data_size - 1, std::move(chunk));
                if (!created)
                    throw error(fmt::format("rollback failed: couldn't reinsert chunk {}", chunk_path));
            }
            _truncated_chunks.clear();
            _unmerged_chunks.clear();
        }

        void _my_commit_tx()
        {
            if (!std::filesystem::exists(_state_path_pre))
                throw error(fmt::format("the prepared chunk_registry state file is missing: {}!", _state_path_pre));
            std::filesystem::rename(_state_path_pre, _state_path);
            for (const auto &chunk: _truncated_chunks)
                _file_remover.mark(full_path(chunk.rel_path()));
            _truncated_chunks.clear();
            for (const auto &[last_byte_offset, chunk]: _chunks)
                _file_remover.unmark(full_path(chunk.rel_path()));
        }

        void _require_better_candidate_chain()
        {
            const auto new_tip = tip();
            if (!new_tip || !(_transaction->start < new_tip))
                throw error(fmt::format("candidate chain is not better: proposed tip: {} intersection: {}", new_tip, _transaction->start));
            if (!_truncated_chunks.empty()) {
                // slot window for the chain density calculation
                const auto win_size = cardano::density_default_window;
                uint64_t win_last_slot = win_size;
                std::optional<cardano::point> isect {};
                size_t orig_num_blocks = 0;
                if (_transaction->start_offset() > 0) {
                    const auto last_common_block = find_block_by_offset(_transaction->start_offset() - 1);
                    isect = cardano::point { last_common_block.hash, last_common_block.slot };
                }
                auto cand_chunk_it = _find_chunk_by_offset(_transaction->start_offset());
                auto cand_block_it = cand_chunk_it->second.blocks.begin();
                bool found = false;
                for (const auto &orig_chunk: _truncated_chunks) {
                    for (const auto &block: orig_chunk.blocks) {
                        if (found) {
                            if (block.slot <= win_last_slot)
                                ++orig_num_blocks;
                        } else if (block.slot != cand_block_it->slot || block.hash != cand_block_it->hash) {
                            found = true;
                            if (block.slot <= win_last_slot)
                                orig_num_blocks = 2;
                        } else {
                            isect = cardano::point { block.hash, block.slot };
                            orig_num_blocks = 1;
                            win_last_slot = block.slot + win_size;
                            if (++cand_block_it == cand_chunk_it->second.blocks.end()) {
                                if (++cand_chunk_it != _chunks.end()) {
                                    cand_block_it = cand_chunk_it->second.blocks.begin();
                                } else {
                                    found = true;
                                }
                            }
                        }
                    }
                }
                // some candidate blocks may have not passed the delayed steps of the validation
                const auto last_valid_block = find_block_by_offset(new_tip->end_offset - 1);
                const auto last_valid_slot = std::min(win_last_slot, static_cast<uint64_t>(last_valid_block.slot));
                const auto cand_num_blocks = count_blocks(isect, last_valid_slot);
                if (orig_num_blocks >= cand_num_blocks)
                    throw error(fmt::format("candidate chain at byte {} and point {} is not better than the original: candidate block count {} vs {}",
                        _transaction->start_offset(), isect, cand_num_blocks, orig_num_blocks));
            }
        }

        // does not report an error if some progress is made
        [[nodiscard]] std::exception_ptr _accept_progress(const cardano::optional_point &start, const std::optional<progress_point> &target,
                const bool aim_progress, const std::function<void()> &action) {
            _start_tx(start, target);
            auto ex_ptr = logger::run_log_errors([&] {
                action();
            });
            if (!ex_ptr || aim_progress) {
                ex_ptr = logger::run_log_errors([&] {
                    _prepare_tx();
                    if (aim_progress)
                        _require_better_candidate_chain();
                    _commit_tx();
                });
            }
            if (ex_ptr) {
                logger::run_log_errors([&] {
                    _rollback_tx();
                });
                // ensure there are no run-away tasks
                logger::run_log_errors([&] {
                    _sched.process(true);
                });
            }
            return ex_ptr;
        }

        void _start_tx(cardano::optional_point start, const std::optional<progress_point> &target)
        {
            timer t { "chunk_registry::start_tx", logger::level::debug };
            if (_transaction)
                throw error("nested transactions are not allowed!");
            if (target < start)
                throw error(fmt::format("the target slot {} cannot be smaller than the start chain {}", target, start));
            if (start) {
                // checks that the requested start point is known
                const auto &block = find_block_by_slot(start->slot, start->hash);
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

        void _prepare_tx()
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

        void _rollback_tx()
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

        void _commit_tx()
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

        void _save_state(const std::string &path)
        {
            // the caller is responsible to hold a lock protecting access to the _chunks!
            zpp::save(path, _chunks);
        }

        void _do_truncate(const cardano::optional_point &new_tip, const bool track_changes)
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

        void _add(chunk_info &&chunk, const bool normal=true);

        void _notify_of_updates(mutex::unique_lock &update_lk, bool force=false)
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

        chunk_map::iterator _find_chunk_by_offset(const uint64_t offset)
        {
            const auto it = _chunks.lower_bound(offset);
            if (it == _chunks.end())
                throw error(fmt::format("no chunk matches offset: {}!", offset));
            return it;
        }

        chunk_map::const_iterator _find_chunk_by_offset_no_throw(const uint64_t offset) const
        {
            return _chunks.lower_bound(offset);
        }

        chunk_map::const_iterator _find_chunk_by_offset(const uint64_t offset) const
        {
            const auto it = _find_chunk_by_offset_no_throw(offset);
            if (it == _chunks.end())
                throw error(fmt::format("no chunk matches offset: {}!", offset));
            return it;
        }

        storage::block_list::const_iterator _find_block_by_offset(const chunk_map::const_iterator chunk_it, const uint64_t offset) const
        {
            if (chunk_it == _chunks.end())
                throw error(fmt::format("internal error: a non-empty chunk_iterator is expected!"));
            const auto &blocks = chunk_it->second.blocks;
            const auto block_it = std::lower_bound(blocks.begin(), blocks.end(), offset,
                [](const auto &b, const auto offset) { return b.end_offset() - 1 < offset; });
            return block_it;
        }

        // can return the closest succeeding chunk if no chunk includes the block
        chunk_map::const_iterator _find_chunk_by_slot(const uint64_t slot) const
        {
            const auto chunk_it = std::lower_bound(_chunks.begin(), _chunks.end(), slot,
                [](const auto &c, const auto &slot) { return c.second.last_slot < slot; });
            return chunk_it;
        }

        // can return the closest succeeding block if there is no block at that slot
        storage::block_list::const_iterator _find_block_by_slot(const chunk_map::const_iterator chunk_it, const uint64_t slot) const
        {
            if (chunk_it == _chunks.end())
                throw error(fmt::format("internal error: a non-empty chunk_iterator is expected!"));
            const auto &blocks = chunk_it->second.blocks;
            const auto block_it = std::lower_bound(blocks.begin(), blocks.end(), slot,
                [](const auto &b, const auto &slot) { return b.slot < slot; });
            return block_it;
        }

        bool _has_epoch(const uint64_t epoch, mutex::unique_lock &update_lk) const
        {
            if (!update_lk)
                throw error("internal error: update lock must be held at the call to _has_epoch");
            const auto chunk_it = std::lower_bound(_chunks.begin(), _chunks.end(), epoch,
                [this](const auto &el, const auto &epoch) { return make_slot(el.second.first_slot).epoch() < epoch; });
            return chunk_it != _chunks.end() && make_slot(chunk_it->second.first_slot).epoch() == epoch;
        }
    };
}

namespace fmt {
    template<>
    struct formatter<daedalus_turbo::progress_point>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "(slot: {} end_offset: {})", v.slot, v.end_offset);
        }
    };
}