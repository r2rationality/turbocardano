#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <turbo/cardano.hpp>
#include <turbo/cardano/common/config.hpp>
#include <turbo/common/progress.hpp>
#include <turbo/common/scheduler.hpp>
#include <turbo/common/timer.hpp>
#include <turbo/file-remover.hpp>
#include <turbo/file.hpp>
#include <turbo/indexer.hpp>
#include <turbo/json.hpp>
#include <turbo/storage/chunk-info.hpp>
#include <turbo/storage/const-iterator.hpp>
#include <turbo/storage/const-reverse-iterator.hpp>
#include <turbo/validator.hpp>
#include <turbo/zpp.hpp>

#include "utfcpp/utf8/checked.h"

namespace turbo {

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
    using epoch_map = std::map<size_t, epoch_info>;

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

        struct repack_stats_t {
            size_t chunks_analyzed = 0;
            size_t chunks_repacked = 0;
            size_t partial_groups_merged = 0;
            uint64_t compressed_size_before = 0;
            uint64_t compressed_size_after = 0;

            repack_stats_t &operator+=(const repack_stats_t &o)
            {
                chunks_analyzed += o.chunks_analyzed;
                chunks_repacked += o.chunks_repacked;
                partial_groups_merged += o.partial_groups_merged;
                compressed_size_before += o.compressed_size_before;
                compressed_size_after += o.compressed_size_after;
                return *this;
            }
        };

        // Shall be a multiple of an SSD's sector size and larger than Cardano's largest block (including Byron boundary ones too!)
        using chunk_info = storage::chunk_info;
        using chunk_map = storage::chunk_map;
        using chunk_list = storage::chunk_list;

        using const_iterator = storage::const_iterator;
        using const_reverse_iterator = storage::const_reverse_iterator;

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
            bool auto_maintenance=true, bool validate_vrf=true);
        ~chunk_registry();

        // Interoperability

        void register_processor(const chunk_processor &p);
        void remove_processor(const chunk_processor &p);
        void report_progress(const std::string_view name, const progress_point &tip) const;

        void validation_failure_handler(const std::function<void(uint64_t)> &);
        const indexer::incremental &indexer() const;
        const validator::incremental &validator() const;

        std::optional<active_transaction> tx() const;
        const cardano::config &config() const;
        scheduler &sched() const;
        file_remover &remover() const;

        // data accessors

        bool empty() const;
        const_iterator cbegin() const;
        const_iterator cend() const;
        const_reverse_iterator crbegin() const;
        const_reverse_iterator crend() const;
        const chunk_map &chunks() const;

        epoch_map epochs() const;
        bool has_epoch(const uint64_t epoch) const;
        cardano::slot make_slot(uint64_t slot_) const;
        uint64_t num_bytes() const;
        uint64_t num_compressed_bytes() const;
        size_t num_blocks() const;

        const_iterator find_by_offset(const uint64_t offset) const;
        std::optional<storage::block_info> find_block_by_offset_no_throw(const uint64_t offset) const;
        storage::block_info find_block_by_offset(const uint64_t offset) const;
        const storage::block_info &find_block_by_slot(const uint64_t slot) const;
        const_iterator latest_block_after_or_at_slot(uint64_t slot) const;
        const_iterator latest_block_before_or_at_slot(uint64_t slot) const;

        const_iterator find_block(const cardano::point2 &p) const;
        storage::block_info get_block_info(const cardano::point2 &p) const;
        uint64_t find_epoch(const uint64_t offset) const;
        const chunk_info &find_offset(uint64_t offset) const;
        const chunk_info &find_last_block_hash(const buffer &last_block_hash) const;
        chunk_map::const_iterator find_offset_it(uint64_t offset) const;

        cardano::amount unspent_reward(const cardano::stake_ident &id) const;
        cardano::tail_relative_stake_map tail_relative_stake() const;

        cardano::optional_point tip() const;
        cardano::optional_point core_tip() const;
        cardano::optional_point immutable_tip() const;

        std::optional<storage::block_info> last_valid_block() const;
        uint64_t max_slot() const;
        uint64_t valid_end_offset() const;
        uint64_t max_end_offset() const;

        // block data access

        const std::filesystem::path &data_dir() const;
        std::string rel_path(const std::filesystem::path &full_path) const;
        std::string full_path(const std::filesystem::path &rel_path) const;
        uint64_t read_holding_chunk(uint8_vector &chunk_data, const uint64_t offset) const;
        cbor::zero2::parsed_value read_from_chunk_buffer(const uint64_t value_offset, const buffer &chunk_data, const uint64_t chunk_offset) const;
        cbor::zero2::parsed_value read(const uint64_t offset) const;

        // state modifying methods

        void maintenance();
        repack_stats_t repack();
        void import(const chunk_registry &src_cr);
        progress_point add_buffer(uint64_t offset, uint8_vector uncompressed, std::optional<uint8_vector> compressed={}, int32_t compression_level=0);
        void add_file(uint64_t offset, const std::string &local_path, int32_t compression_level=0);
        [[nodiscard]] std::exception_ptr accept_progress(const cardano::optional_point &start, const std::optional<progress_point> &target, const std::function<void()> &action);
        void accept_anything_or_throw(const cardano::optional_point &start, const std::optional<progress_point> &target, const std::function<void()> &action);
        //void accept_progress_or_throw(const cardano::optional_point &start, const std::optional<progress_point> &target, const std::function<void()> &action);
        void truncate(const cardano::optional_point &new_tip);

        // data export

        cardano::optional_slot can_export() const;
        void node_export(const std::filesystem::path &node_dir, const cardano::point &tip, bool ledger_only=false) const;
        std::string node_export_ledger(const std::filesystem::path &ledger_dir, const cardano::optional_point &imm_tip, int prio=1000) const;
    private:
        friend const_iterator;

        const std::filesystem::path _data_dir;
        const std::filesystem::path _db_dir;
        const cardano::config _cardano_cfg;
        scheduler &_sched;
        file_remover &_file_remover;
        mutable mutex::unique_lock::mutex_type _processors_mutex alignas(mutex::alignment) {};
        std::set<const chunk_processor *> _processors {}; // initialize before indexer and validator who call register_processor/remove_processor
        std::unique_ptr<indexer::incremental> _indexer {};
        std::unique_ptr<validator::incremental> _validator {};
        std::optional<active_transaction> _transaction {};
        mutable mutex::unique_lock::mutex_type _tx_progress_mutex alignas(mutex::alignment) {};
        mutable std::map<std::string, uint64_t> _tx_progress_max {};
        mutable std::atomic_size_t _tx_progress_parse { 0 };
        const std::string _state_path;
        const std::string _state_path_pre;
        mutable mutex::unique_lock::mutex_type _update_mutex alignas(mutex::alignment) {};
        chunk_map _chunks {};
        // Active transaction data
        chunk_map _unmerged_chunks {};
        uint64_t _notify_end_offset = 0;
        uint64_t _notify_next_epoch = 0;
        chunk_map _truncated_chunks {};
        static thread_local uint8_vector _read_buffer;

        void _node_export_chain(const std::filesystem::path &immutable_dir, const std::filesystem::path &volatile_dir, int prio_base=100) const;
        std::pair<chunk_info, std::exception_ptr> _parse(uint64_t offset, const buffer &raw_data, size_t compressed_size,
            int32_t compression_level, const std::optional<cardano::block_hash> &data_hash) const;

        epoch_info _epoch(const uint64_t epoch) const;
        void _my_truncate(const cardano::optional_point &new_tip, const bool track_changes);
        void _my_start_tx();
        uint64_t _my_end_offset() const;
        void _my_prepare_tx();
        void _my_rollback_tx();
        void _my_commit_tx();
        void _require_better_candidate_chain();
        // can commit progress while still returning the error that stopped the attempt
        [[nodiscard]] std::exception_ptr _accept_progress(const cardano::optional_point &start, const std::optional<progress_point> &target,
                const bool aim_progress, const std::function<void()> &action);
        void _start_tx(cardano::optional_point start, const std::optional<progress_point> &target);
        void _prepare_tx();
        void _rollback_tx();
        void _commit_tx();
        void _save_state(const std::string &path);
        void _do_truncate(const cardano::optional_point &new_tip, const bool track_changes);
        progress_point _add(const uint64_t offset, const std::string &local_path, buffer uncompressed,
            uint64_t compressed_size, int32_t compression_level, std::optional<cardano::block_hash> data_hash={});
        void _add(chunk_info &&chunk, const bool normal=true);
        void _notify_of_updates(mutex::unique_lock &update_lk, bool force=false);

        chunk_map::iterator _find_chunk_by_offset(const uint64_t offset);
        chunk_map::const_iterator _find_chunk_by_offset_no_throw(const uint64_t offset) const;
        chunk_map::const_iterator _find_chunk_by_offset(const uint64_t offset) const;
        storage::block_list::const_iterator _find_block_by_offset(const chunk_map::const_iterator chunk_it, const uint64_t offset) const;
        // can return the closest succeeding chunk if no chunk includes the block
        chunk_map::const_iterator _find_chunk_by_slot(const uint64_t slot) const;
        // can return the closest succeeding block if there is no block at that slot
        storage::block_list::const_iterator _find_block_by_slot(const chunk_map::const_iterator chunk_it, const uint64_t slot) const;
        bool _has_epoch(const uint64_t epoch, mutex::unique_lock &update_lk) const;
    };
}

namespace fmt {
    template<>
    struct formatter<turbo::progress_point>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "(slot: {} end_offset: {})", v.slot, v.end_offset);
        }
    };
}
