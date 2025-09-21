/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cli/common.hpp>
#include <turbo/bech32.hpp>

namespace turbo::cli::debug::bech32_parse {
    struct cmd: command {
        const command_info &info() const override
        {
            static const command_info i { "bech32-parse", "<bech32-value> [<bech32-value>]", "convert BECH32-encoded string to a hexademical string" };
            return i;
        }

        void run(const arguments &args) const override
        {
            if (args.size() < 1) _throw_usage();
            for (const auto &arg: args) {
                bech32 val { arg, false };
                std::cout << arg << " => " << fmt::format("{}", static_cast<buffer>(val)) << '\n';
            }            
        }
    };

    static auto instance = command::reg(std::make_shared<cmd>());
}