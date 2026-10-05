/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <turbo/cbor/normalize.hpp>
#include <turbo/common/cli.hpp>
#include <turbo/common/file.hpp>

namespace turbo::cli::test_cbor_normalization {
    namespace fs = std::filesystem;

    struct cmd: command {
        void configure(config &cmd) const override {
            cmd.name = "test-cbor-normalize";
            cmd.desc = "normalize .input.cbor files and compare them with .expected.cbor files";
            cmd.args.expect({ "<sample-dir>" });
        }

        void run(const arguments &args) const override {
            const fs::path sample_dir{args.at(0)};
            if (!fs::is_directory(sample_dir)) [[unlikely]]
                throw error{"expected a sample directory: {}", sample_dir};

            static constexpr std::string_view in_ext{".input.cbor"};
            static constexpr std::string_view exp_ext{".expected.cbor"};
            size_t total = 0;
            size_t failed = 0;
            for (const auto &entry: fs::recursive_directory_iterator(sample_dir)) {
                const auto path = entry.path().string();
                if (!entry.is_regular_file() || !path.ends_with(in_ext))
                    continue;
                ++total;
                try {
                    const auto input = file::read(path);
                    const auto expected = file::read(std::string{path}.replace(path.size() - in_ext.size(), in_ext.size(), exp_ext));
                    const auto actual = cbor::normalize(input);
                    if (actual != expected)
                        throw error{"{}: normalized CBOR differs", path};
                } catch (const std::exception &ex) {
                    ++failed;
                    logger::error("{}: {}", path, ex.what());
                }
            }
            if (total == 0) [[unlikely]]
                throw error{"no .input.cbor samples found in {}", sample_dir};
            if (failed != 0)
                throw error{"CBOR normalization: {} of {} samples failed", failed, total};
            logger::info("CBOR normalization: all {} samples passed", total);
        }
    };

    static auto instance = command::reg(std::make_shared<cmd>());
}
