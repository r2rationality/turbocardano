/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cli/common.hpp>
#include <turbo/cbor/zero2.hpp>
#include <turbo/common/format.hpp>
#include <turbo/file.hpp>

namespace turbo::cli::debug::cbor_view {
    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "cbor-view";
            cmd.desc = "debug print CBOR from a given file, optionally constrained to a given value-path";
            cmd.args.expect({ "<cbor-path>", "[<value-path>]" });
        }

        void run(const arguments &args) const override
        {
            // force buffering on stdout to optimize output performance
            setvbuf(stdout, 0, _IOFBF, 65536);
            const std::string &path = args[0];
            std::vector<size_t> value_path {};
            if (args.size() > 1) {
                value_path = cbor::parse_value_path(args[1]);
                fmt::print("value_path: {}\n", value_path);
            }
            const auto buf = path.starts_with('#') ? uint8_vector::from_hex(path.substr(1)) : file::read_auto(path);
            std::cout << "loaded " << path << " into RAM, size: " << buf.size() / 1'000'000u << " MB\n";
            const auto block_hash = crypto::blake2b::digest(buf);
            std::cout << fmt::format("STREAM blake2b-256 hash {}\n", block_hash);
            _view_zero2(buf, value_path);
        }
    private:
        static void _view_zero2(const buffer data, const std::vector<size_t> &value_path)
        {
            cbor::zero2::decoder dec { data };
            for (size_t i = 0; !dec.done(); ++i) {
                auto &v = dec.read();
                if (value_path.empty() || i == value_path[0]) {
                    std::cout << fmt::format("STREAM #{} ", i);
                    if (!value_path.empty())
                         cbor::zero2::extract(v, value_path, 1).to_stream(std::cout);
                    else
                        v.to_stream(std::cout, 30);
                    std::cout << fmt::format("\nSTREAM #{} size: {}\n", i, v.data_raw().size());
                }
            }
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}