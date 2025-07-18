/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/chunk-registry.hpp>
#include <turbo/common/test.hpp>
#include <turbo/index/txo-use.hpp>
#include <turbo/indexer.hpp>

using namespace turbo;
using namespace turbo::indexer;

suite indexer_suite = [] {
    "indexer"_test = [] {
        static const std::string src_dir { "./data/chunk-registry" };
        static const std::string data_dir { "./tmp/indexer" };
        static const auto idx_dir = indexer::incremental::storage_dir(data_dir);
        "create"_test = [&] {
            std::filesystem::remove_all(data_dir);
            {
                chunk_registry src_cr { src_dir, chunk_registry::mode::store };
                chunk_registry idxr { data_dir, chunk_registry::mode::index };
                idxr.import(src_cr);
            }
            {
                chunk_registry idxr { data_dir, chunk_registry::mode::index };
                index::reader_multi<index::txo_use::item> reader { idxr.indexer().reader_paths("txo-use") };
                index::txo_use::item i {};
                size_t read_count = 0;
                while (reader.read(i)) {
                    ++read_count;
                }
                expect(read_count == 244'802_ull) << read_count;
                expect(read_count == reader.size());
            }
        };
        "rollback"_test = [&] {
            std::filesystem::remove_all(data_dir);
            chunk_registry src_cr { src_dir, chunk_registry::mode::store };
            chunk_registry idxr { data_dir, chunk_registry::mode::index };
            idxr.import(src_cr);
            const auto before_tip = idxr.tip();
            const cardano::point mid_point = idxr.find_block_by_offset(idxr.num_bytes() / 2).point();
            expect(!!idxr.accept_progress(mid_point, mid_point, [] {
                throw error("something went wrong");
            }));
            expect(!idxr.tx());
            expect_equal(idxr.tip(), before_tip);
            idxr.truncate(mid_point);
            expect_equal(idxr.tip(), mid_point);
        };
    };    
};