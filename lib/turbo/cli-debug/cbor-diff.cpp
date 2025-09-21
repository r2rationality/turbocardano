/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cli/common.hpp>
#include <turbo/cbor/compare.hpp>
#include <turbo/common/scheduler.hpp>

namespace turbo::cli::cbor_diff {
    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "cbor-diff";
            cmd.desc = "recursively compare two CBOR files by value and print the first difference";
            cmd.args.expect({ "<path-expected>", "<path-actual>" });
        }

        void run(const arguments &args) const override
        {
            const auto &path1 = args.at(0);
            const auto &path2 = args.at(1);
            uint8_vector expected {}, actual {};
            {
                timer t1 { "load", logger::level::info };
                auto &sched = scheduler::get();
                sched.submit("load-orig", 100, [&] {
                    file::read(path1, expected);
                });
                sched.submit("load-gen", 100, [&] {
                    file::read(path2, actual);
                });
                sched.process();
            }
            const auto res = cbor::compare(expected, actual);
            logger::info("compare result: {}", res);
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}