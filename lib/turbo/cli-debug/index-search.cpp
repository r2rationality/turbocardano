/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cli/common.hpp>
#include <turbo/chunk-registry.hpp>
#include <turbo/index/pay-ref.hpp>
#include <turbo/index/stake-ref.hpp>
#include <turbo/index/tx.hpp>
#include <turbo/index/txo-use.hpp>

namespace turbo::cli::debug::index_search {
    struct cmd: command {
        const command_info &info() const override
        {
            static const command_info i { "index-search", "<data-dir> <index-type> <search-bytes>", "search an index for a byte string" };
            return i;
        }

        void run(const arguments &args) const override
        {
            if (args.size() < 3)
                _throw_usage();
            const auto &data_dir = args.at(0);
            const auto &index_type = args.at(1);
            const auto &search_bytes_hex = args.at(2);
            if (index_type == "stake-ref") {
                _search(data_dir, index_type, index::stake_ref::item { cardano::stake_ident { cardano::key_hash::from_hex(search_bytes_hex), false } });
            } else if (index_type == "tx") {
                _search(data_dir, index_type, index::tx::item { cardano::tx_hash::from_hex(search_bytes_hex) });
            } else if (index_type == "txo-use") {
                _search(data_dir, index_type, index::txo_use::item { cardano::tx_hash::from_hex(search_bytes_hex), 0 });
            } else {
                throw error(fmt::format("unknown index type: {}", index_type));
            }
        }
    private:
        template<typename T>
        static void _search(const std::string &data_dir, const std::string &idx_name, const T &search_item)
        {
            chunk_registry cr { data_dir, chunk_registry::mode::index };
            index::reader_multi<T> reader { cr.indexer().reader_paths(idx_name) };
            auto [ref_count, ref_item] = reader.find(search_item);
            logger::info("found {} matches", ref_count);
            for (size_t i = 0; i < ref_count; i++) {
                if (i > 0) reader.read(ref_item);
                logger::info("#{}: {}", i, ref_item);
            }
        }
    };

    static auto instance = command::reg(std::make_shared<cmd>());
}