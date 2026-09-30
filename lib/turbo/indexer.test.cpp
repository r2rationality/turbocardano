/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/chunk-registry.hpp>
#include <turbo/common/test.hpp>
#include <turbo/index/txo-use.hpp>
#include <turbo/indexer.hpp>
#include <turbo/indexer/test.hpp>
#include <turbo/sync/mocks.hpp>
#include <turbo/storage/test.hpp>

using namespace turbo;
using namespace turbo::indexer;

suite indexer_suite = [] {
    "indexer"_test = [] {
        static const std::string src_dir { turbo::storage::sample_registry_path() };
        static const std::string data_dir { "./tmp/indexer" };
        "create"_test = [&] {
            std::filesystem::remove_all(data_dir);
            {
                chunk_registry src_cr { src_dir, chunk_registry_settings_t { .mode=chunk_registry::mode::store } };
                create_indexed_fixture(src_cr, data_dir);
            }
            {
                chunk_registry idxr { data_dir, chunk_registry_settings_t { .mode=chunk_registry::mode::index } };
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
        "retained slices survive rollback and reuse of their paths"_test = [] {
            const auto chain = sync::gen_chain({ .height=1 });
            for (const bool existing: { false, true }) {
                const auto context = existing ? "restored slice" : "retried slice";
                const file::tmp_directory dir { "indexer-retained-slices" };
                file_remover remover;
                const chunk_registry_settings_t settings { .ccfg=chain.cardano_cfg, .fr=remover };
                chunk_registry cr { dir.path(), settings };
                const auto ingest = [&] {
                    cr.accept_anything_or_throw({}, *chain.tip, [&] { cr.add_buffer(0, chain.data); });
                };
                if (existing) {
                    ingest();
                }
                const auto original_paths = cr.indexer().reader_paths("tx");
                std::map<std::string, uint8_vector> original_files;
                for (const auto &path: original_paths)
                    original_files.emplace(path, file::read(path));
                slice_path_list staged_paths, live_paths;
                cr.before_commit([&] {
                    staged_paths = cr.indexer().reader_paths("tx");
                    live_paths = multi_reader_paths(cr.indexer().idx_dir().string(), "tx", cr.indexer().slices());
                    expect(staged_paths != live_paths) << context;
                    for (const auto &path: staged_paths)
                        expect(std::filesystem::is_regular_file(path)) << context;
                    throw error("injected commit failure");
                });
                expect(throws(ingest)) << context;
                expect(fatal(!staged_paths.empty())) << context;
                expect(!cr.tx()) << context;
                // Rollback discards staging directly and restores the committed slices.
                for (const auto &path: staged_paths) {
                    expect(!std::filesystem::exists(path)) << context;
                    expect(!remover.removable().contains(path)) << context;
                }
                expect(cr.indexer().reader_paths("tx") == original_paths) << context;
                remover.remove();
                for (const auto &[path, bytes]: original_files)
                    expect(file::read(path) == bytes) << context;
                for (const auto &path: live_paths)
                    expect(std::filesystem::is_regular_file(path) == existing) << context;

                slice_path_list retry_paths;
                cr.before_commit([&] {
                    retry_paths = cr.indexer().reader_paths("tx");
                    expect(retry_paths != staged_paths) << context;
                });
                ingest();
                cr.before_commit({});
                expect(cr.indexer().reader_paths("tx") == live_paths) << context;
                remover.remove();
                for (const auto &path: retry_paths)
                    expect(!std::filesystem::exists(path)) << context;
                for (const auto &path: live_paths) {
                    expect(std::filesystem::is_regular_file(path)) << context;
                    expect(!remover.removable().contains(path)) << context;
                }
                chunk_registry reopened { dir.path(), chunk_registry_settings_t {
                    .mode=chunk_registry::mode::index, .ccfg=chain.cardano_cfg, .fr=remover
                } };
                expect_equal(reopened.tip(), cr.tip(), context);
                expect(reopened.indexer().reader_paths("tx") == live_paths) << context;
            }
        };

        "rollback"_test = [&] {
            std::filesystem::remove_all(data_dir);
            const auto chain = sync::gen_chain({ .height=3 });
            chunk_registry idxr { data_dir, chunk_registry_settings_t { .ccfg=chain.cardano_cfg } };
            idxr.accept_anything_or_throw({}, *chain.tip, [&] { idxr.add_buffer(0, chain.data); });
            const auto before_tip = idxr.tip();
            const cardano::point mid_point = idxr.find_block_by_offset(idxr.num_bytes() / 2).point();
            expect(!!idxr.accept_progress(mid_point, mid_point, [] {
                throw error("something went wrong");
            }));
            expect(!idxr.tx());
            expect_equal(idxr.tip(), before_tip);
            expect_equal(idxr.chunks().size(), 1);
            expect_equal(idxr.num_blocks(), chain.blocks.size());
            expect_equal(idxr.valid_end_offset(), chain.data.size());
            idxr.truncate(mid_point);
            expect_equal(idxr.tip(), mid_point);
        };
    };    
};
