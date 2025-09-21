/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cli/common.hpp>
#include <turbo/math/big-int.hpp>

namespace turbo::cli::debug::big_int_view {
    struct cmd: command {
        const command_info &info() const override
        {
            static const command_info i { "big-int-view", "<hex-encoded-varint>", "print variable-length big int" };
            return i;
        }

        void run(const arguments &args) const override
        {
            if (args.size() != 1)
                _throw_usage();
            const auto buf = uint8_vector::from_hex(args.at(0));
            const auto big_int = big_uint_from_bytes(buf);
            logger::info("{}", big_int);
        }
    };

    static auto instance = command::reg(std::make_shared<cmd>());
}