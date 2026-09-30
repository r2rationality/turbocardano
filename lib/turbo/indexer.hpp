#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#ifndef _WIN32
#   include <sys/resource.h>
#endif
#include <array>
#include <turbo/file-remover.hpp>
#include <turbo/storage/chunk-info.hpp>
#include <turbo/common/progress.hpp>
#include <turbo/common/scheduler.hpp>
#include <turbo/common/logger.hpp>
#include <turbo/index/common.hpp>
#include <turbo/indexer/merger.hpp>

namespace turbo {
    struct chunk_registry;
}

namespace turbo::indexer {
    struct index_layout_t {
        static constexpr const char *stake_ref = "stake-ref";
        static constexpr const char *pay_ref = "pay-ref";
        static constexpr const char *tx = "tx";
        static constexpr const char *txo_use = "txo-use";
        static constexpr const char *block_fees = "block-fees";
        static constexpr const char *timed_update = "timed-update";
        static constexpr const char *vrf = "vrf";
        static constexpr const char *utxo = "utxo";
        static constexpr std::array query { stake_ref, pay_ref, tx, txo_use };
        static constexpr std::array ledger { block_fees, timed_update, vrf, utxo };
        static constexpr std::array auxiliary { "epoch-delta", "outflow" };
    };

    struct chunk_indexer_list_t: std::vector<std::shared_ptr<index::chunk_indexer_base>> {
        using std::vector<std::shared_ptr<index::chunk_indexer_base>>::vector;

        void index_block(const cardano::block_container &block) const;
    };
    using slice_list = std::vector<merger::slice>;
    using slice_path_list = std::vector<std::string>;

    struct indexer_map: std::map<std::string, std::shared_ptr<index::indexer_base>> {
        using std::map<std::string, std::shared_ptr<index::indexer_base>>::map;

        void emplace(std::shared_ptr<index::indexer_base> &&idxr) {
            auto [ it, created ] = try_emplace(idxr->name(), std::move(idxr));
            if (!created) [[unlikely]]
                throw error(fmt::format("duplicate index: {}", it->first));
        }
    };

    extern slice_path_list multi_reader_paths(const std::string &idx_dir, const std::string &name, const slice_list &slices);
    extern indexer_map default_list(const std::string &data_dir, scheduler &sched=scheduler::get());
    slice_list inspect_state(const std::filesystem::path &data_dir, const storage::chunk_map &chunks,
        const file_remover::remove_point_map &marked, bool required);
    void clean_up_ledger_data(const indexer_map &indexers, const std::filesystem::path &index_dir);

    struct incremental {
        static std::string storage_dir(const std::string &data_dir);

        incremental(chunk_registry &cr, indexer_map &&indexers, const storage::chunk_map &chunks, std::optional<slice_list> slices={});
        ~incremental();
        chunk_indexer_list_t make_chunk_indexers(uint64_t chunk_offset);
        slice_list slices(std::optional<uint64_t> end_offset={}) const;
        slice_path_list reader_paths(const std::string &name, const slice_list &slcs) const;
        slice_path_list reader_paths(const std::string &name) const;
        const indexer_map &indexers() const;
        const std::filesystem::path &idx_dir() const;
    protected:
        struct impl;
        std::unique_ptr<impl> _impl;
    };
}
