/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cli/common.hpp>
#include <turbo/index/pay-ref.hpp>
#include <turbo/index/stake-ref.hpp>
#include <turbo/index/tx.hpp>
#include <turbo/index/txo-use.hpp>
#include <turbo/indexer.hpp>

namespace turbo::cli::debug::index_validate {
    struct cmd: command {
        const command_info &info() const override
        {
            static const command_info i { "index-validate", "<data-dir> <index-type> [<chunk-id>]", "validate an index is written in a monotonically increasing order" };
            return i;
        }

        void run(const arguments &args) const override
        {
            if (args.size() < 2) _throw_usage();
            const std::filesystem::path data_dir { args.at(0) };
            const std::filesystem::path index_dir { indexer::incremental::storage_dir(data_dir.string()) };
            const auto &index_type = args.at(1);
            std::string chunk_id { "index" };
            if (args.size() > 2)
                chunk_id = args.at(2);
            const auto index_path = std::filesystem::weakly_canonical(index_dir / index_type / chunk_id).string() + ".data";
            if (index_type == "pay-ref") _validate_index<index::pay_ref::item>(index_path);
            else if (index_type == "stake-ref") _validate_index<index::stake_ref::item>(index_path);
            else if (index_type == "tx") _validate_index<index::tx::item>(index_path, true);
            else if (index_type == "txo-use") _validate_index<index::txo_use::item>(index_path, true);
            else throw error(fmt::format("unknown index type: {}", index_type));
        }
    private:
        template<typename T>
        static std::string _make_nonunique_err(const T &, const T &, size_t idx)
        {
            return fmt::format("item {}: redundant element in an index expecting unique entries", idx);
        }

        static std::string _make_nonunique_err(const index::txo_use::item &prev, const index::txo_use::item &next, size_t idx)
        {
            return fmt::format("item {}: reuse of tx output {} #{} at offsets {} and {}",
                idx, prev.hash, prev.out_idx, prev.offset, next.offset);
        }

        template<typename T>
        static void _validate_index(const std::string &idx_path, bool unique=false)
        {
            index::reader<T> reader { idx_path };
            if (reader.eof()) return;
            T prev {}, next {};
            std::vector<std::string> errors {};
            reader.read(prev);
            size_t pos = 1;
            while (reader.read(next)) {
                pos++;
                if (next < prev) {
                    auto err = fmt::format("item {}: wrong element order", pos);
                    logger::error(err);
                    errors.emplace_back(std::move(err));
                }
                if (unique && prev == next) {
                    auto  err = _make_nonunique_err(prev, next, pos);
                    logger::error(err);
                    errors.emplace_back(std::move(err));
                }
                prev = next;
            }
            logger::info("validated {} entries and found {} errors", pos, errors.size());
        }
    };

    static auto instance = command::reg(std::make_shared<cmd>());
}