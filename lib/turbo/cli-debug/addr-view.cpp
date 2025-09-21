/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cli/common.hpp>
#include <turbo/history.hpp>

namespace turbo::cli::debug::addr_view {
    struct cmd: command {
        const command_info &info() const override
        {
            static const command_info i { "addr-view", "<hex-encoded-address>", "print structured information about an address in its binary format" };
            return i;
        }

        void run(const arguments &args) const override
        {
            if (args.size() != 1) _throw_usage();
            auto addr_buf = uint8_vector::from_hex(args.at(0));
            const cardano::address addr { addr_buf };
            logger::info("{}", addr);
            if (addr.is_byron())
                logger::info("byron-addr: {}", addr.byron());
        }
    };

    static auto instance = command::reg(std::make_shared<cmd>());
}