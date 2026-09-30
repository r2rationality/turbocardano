/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cli/common.hpp>
#include <turbo/chunk-registry.hpp>
#include <turbo/storage/test.hpp>

namespace turbo::cli::test_recreate_cr_state {
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
            using namespace turbo;
            const auto &src_dir = *opts.at("src-dir");
            const auto &dst_dir = *opts.at("dst-dir");
            std::filesystem::remove_all(dst_dir);
            const auto j_state = json::load(src_dir + "/compressed/state.json").as_object();
            storage::chunk_fixture_t fixture { dst_dir };
            uint64_t offset = 0;
            for (const auto &j_chunk: j_state.at("chunks").as_array()) {
                const auto chunk = storage::chunk_info::from_json(j_chunk.as_object());
                const auto src_path = src_dir + "/compressed/" + chunk.rel_path();
                const auto compressed = file::read(src_path);
                const auto raw = zstd::decompress(compressed);
                fixture.add_trusted(offset, raw, compressed);
                offset += raw.size();
            }
            fixture.save();
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}
