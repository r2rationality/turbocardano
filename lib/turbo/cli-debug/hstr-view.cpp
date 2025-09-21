/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cli/common.hpp>
#include <turbo/cardano/common/types.hpp>

namespace turbo::cli::hstr_view {
    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "hstr-view";
            cmd.desc = "print a Haskell-encoded binary string as hexadecimal string";
            cmd.args.expect({ "<haskell-binary-string>" });
        }

        void run(const arguments &args) const override
        {
            logger::info("{}", cardano::from_haskell(args.at(0)));
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}