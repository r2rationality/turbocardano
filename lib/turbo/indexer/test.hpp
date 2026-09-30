#pragma once

#include <turbo/chunk-registry.hpp>
#include <turbo/common/scope-exit.hpp>
#include <turbo/storage/replay.hpp>

namespace turbo::indexer {
    inline void create_indexed_fixture(const chunk_registry &source, const std::string &directory)
    {
        std::filesystem::create_directories(directory);
        std::filesystem::copy(source.data_dir() / "compressed", std::filesystem::path { directory } / "compressed",
            std::filesystem::copy_options::recursive);
        auto indexers = default_list(directory, source.sched());
        for (const auto &[offset, chunk]: source.chunks()) {
            chunk_indexer_list_t writers;
            for (const auto &[name, indexer]: indexers) {
                writers.emplace_back(indexer->make_chunk_indexer("update", chunk.offset));
            }
            const auto raw = zstd::read(source.full_path(chunk.rel_path()));
            storage::block_reader_t reader { raw, chunk.offset, source.config() };
            while (!reader.done()) {
                writers.index_block(reader.next());
            }
        }
        const merger::slice slice { 0, source.num_bytes(), source.max_slot() };
        std::vector<std::string> temporary;
        scope_exit drain { [&] { logger::run_log_errors([&] { source.sched().process(true); }); } };
        for (const auto &[name, indexer]: indexers) {
            std::vector<std::string> inputs;
            for (const auto &[offset, chunk]: source.chunks()) {
                inputs.emplace_back(indexer->chunk_path("update", chunk.offset));
            }
            temporary.insert(temporary.end(), inputs.begin(), inputs.end());
            indexer->merge("index-fixture", 100, inputs, indexer->reader_path(slice.slice_id), [] {});
        }
        source.sched().process(true);
        drain.release();
        for (const auto &path: temporary) {
            std::filesystem::remove(path);
        }
        json::save_pretty(directory + "/index/state.json", json::array { slice.to_json() });
    }
}
