/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cli/common.hpp>
#include <turbo/cbor/zero2.hpp>
#include <turbo/file.hpp>

namespace turbo::cli::debug::cbor_extract {
    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "cbor-extract";
            cmd.desc = "extract a chunk of CBOR data to a separate file";
            cmd.args.expect({ "<input-path>", "<output-path>", "<value-path>" });
        }

        void run(const arguments &args) const override
        {
            const std::string &in_path { args.at(0) }, &out_path { args.at(1) };
            const auto value_path = cbor::parse_value_path(args.at(2));
            const auto buf = file::read_auto(in_path);
            cbor::zero2::decoder dec { buf };
            for (size_t i = 0; !dec.done(); ++i) {
                auto &v = dec.read();
                if (i == value_path[0]) {
                    auto &target = cbor::zero2::extract(v, value_path, 1);
                    file::write(out_path, target.data_raw());
                    break;
                }
            }
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}