#pragma once

#include <random>
#include <turbo/storage/replay.hpp>
#include <turbo/config.hpp>
#include <turbo/zpp.hpp>

namespace turbo::storage {
    inline const std::string &sample_registry_path()
    {
        static const file::tmp_directory directory { [] {
            std::random_device random;
            for (;;) {
                const auto name = fmt::format("chunk-registry-sample-{:08x}{:08x}", random(), random());
                if (std::filesystem::create_directory(std::filesystem::temp_directory_path() / name)) {
                    return name;
                }
            }
        }() };
        static const auto path = [&] {
            std::filesystem::copy(std::filesystem::path { install_path("data/chunk-registry") } / "compressed",
                std::filesystem::path { directory.path() } / "compressed", std::filesystem::copy_options::recursive);
            return directory.path();
        }();
        return path;
    }

    struct chunk_fixture_t {
        explicit chunk_fixture_t(std::string directory, const cardano::config &cfg=cardano::config::get())
            : _directory { std::move(directory) }, _cfg { cfg }
        {
            std::filesystem::create_directories(_directory + "/compressed/chunk");
        }

        void add(const uint64_t offset, const buffer raw)
        {
            const auto compressed = zstd::compress(raw);
            add_trusted(offset, raw, compressed);
        }

        void add_trusted(const uint64_t offset, const buffer raw, const buffer compressed)
        {
            chunk_info chunk {
                .offset=offset,
                .data_size=raw.size(),
                .data_hash=crypto::blake2b::digest<cardano::block_hash>(raw),
                .compressed_size=compressed.size()
            };
            block_reader_t reader { raw, offset, _cfg };
            while (!reader.done()) {
                const auto block = reader.next();
                if (chunk.blocks.empty()) {
                    chunk.first_slot = block->slot();
                    chunk.prev_block_hash = block->prev_hash();
                }
                chunk.blocks.emplace_back(block_info::from_block(block));
                chunk.last_block = { block->slot(), block->hash() };
            }
            chunk.num_blocks = chunk.blocks.size();
            file::write(_directory + "/compressed/" + chunk.rel_path(), compressed);
            _chunks.emplace(chunk.end_offset() - 1, std::move(chunk));
        }

        void save() const
        {
            turbo::zpp::save(_directory + "/compressed/state.bin", _chunks);
        }
    private:
        std::string _directory;
        const cardano::config &_cfg;
        chunk_map _chunks;
    };
}
