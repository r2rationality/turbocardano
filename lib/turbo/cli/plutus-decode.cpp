/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/config.hpp>
#include <turbo/plutus/flat.hpp>
#include <turbo/plutus/machine.hpp>
#include <turbo/plutus/uplc.hpp>
#include "common.hpp"

namespace turbo::cli::plutus_parse {
    using namespace cardano;
    using namespace turbo::plutus;

    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "plutus-decode";
            cmd.desc = "parse a Plutus script in the Flat format and print it as a UPLC";
            cmd.args.expect({ "<script-path>" });
            cmd.opts.try_emplace("cbor", "interpret the byte stream as a CBOR bytestring");
        }

        void run(const arguments &args, const options &opts) const override
        {
            allocator alloc {};
            flat::script s { alloc, file::read(args.at(0)), opts.contains("cbor") };
            fmt::print("{}\n", s.program());
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}