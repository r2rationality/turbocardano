/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cli/common.hpp>
#include <turbo/common/timer.hpp>
#include <turbo/crypto/blake2b.hpp>
#include <turbo/file.hpp>

namespace turbo::cli::debug::blake2b {
    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "blake2b";
            cmd.desc = "compute a Blake2b hash of the <file>'s data";
            cmd.args.expect({ "<file|hex-string>" });
            cmd.opts.try_emplace("bits", "224 or 256 bits", "256");
            cmd.opts.try_emplace("hex", "interpret the file's contents as a hex string");
        }

        void run(const arguments &args, const options &opts) const override
        {
            const auto &path_or_hex = args.at(0);
            uint8_vector data {};
            if (std::filesystem::exists(path_or_hex)) {
                data = file::read(path_or_hex);
                if (const auto opt_it = opts.find("hex"); opt_it != opts.end())
                    data = uint8_vector::from_hex(data.str());
            } else {
                data = uint8_vector::from_hex(path_or_hex);
            }
            switch (const auto bits = std::stoull(opts.at("bits").value()); bits) {
                case 224:  logger::info("blake2b-224: {}", crypto::blake2b::digest(data)); break;
                case 256:  logger::info("blake2b-256: {}", crypto::blake2b::digest(data)); break;
                default: throw error(fmt::format("an unsupported number of bits: {}", bits));
            }
        }
    };

    static auto instance = command::reg(std::make_shared<cmd>());
}