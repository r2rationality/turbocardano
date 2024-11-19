/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2024 Alex Sierkov (alex dot sierkov at gmail dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/cardano/config.hpp>
#include <dt/cli.hpp>
#include <dt/plutus/flat-encoder.hpp>
#include <dt/plutus/uplc.hpp>

namespace daedalus_turbo::cli::plutus_encode {
    using namespace plutus;
    using namespace cardano;
    using namespace daedalus_turbo::plutus;

    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "plutus-encode";
            cmd.desc = "encode a Plutus script into the cbor-encoded Flat format";
            cmd.args.expect({ "<uplc-path>", "<flat-path>" });
        }

        void run(const arguments &args) const override
        {
            const auto &uplc_path = args.at(0);
            const auto &flat_path = args.at(1);
            allocator alloc {};
            const uplc::script s { alloc, file::read(uplc_path) };
            const auto bytes = flat::encode_cbor(s.version(), s.program());
            file::write(args.at(1), bytes);
            logger::info("saved the flat encoded script to file: {} size: {}", flat_path, bytes.size());
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}
