/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */
#include <dt/chunk-registry.hpp>
#include <dt/cli.hpp>

namespace daedalus_turbo::cli::test_recreate_cr_state {
    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "test-recreate-cr-state";
            cmd.desc = "recreate the example binary state of the chunk registry used in unit tests";
            cmd.opts.try_emplace("src-dir", "the directory with the source json file", "./data/chunk-registry");
            cmd.opts.try_emplace("dst-dir", "the directory into which to write new files", "./tmp/test-chunk-registry");
        }

        void run(const arguments &/*args*/, const options &opts) const override
        {
            using namespace daedalus_turbo;
            const auto &src_dir = *opts.at("src-dir");
            const auto &dst_dir = *opts.at("dst-dir");
            std::filesystem::remove_all(dst_dir);
            chunk_registry cr { dst_dir, chunk_registry::mode::store };
            const auto j_state = json::load(src_dir + "/compressed/state.json").as_object();
            cr.accept_anything_or_throw({}, json::value_to<uint64_t>(j_state.at("chunks").as_array().back().at("lastSlot")), [&] {
                uint64_t offset = 0;
                for (const auto &j_chunk: j_state.at("chunks").as_array()) {
                    const auto chunk_name = fmt::format("{}.zstd", json::value_to<std::string_view>(j_chunk.at("hash")));
                    const auto src_path = fmt::format("{}/compressed/chunk/{}", src_dir, chunk_name);
                    const auto dst_path = cr.full_path(chunk_name);
                    std::filesystem::copy(src_path, dst_path);
                    cr.add(offset, dst_path);
                    offset += json::value_to<uint64_t>(j_chunk.at("size"));
                }
            });
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}