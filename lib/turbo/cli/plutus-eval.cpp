/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/plutus/flat-encoder.hpp>
#include <turbo/plutus/flat.hpp>
#include <turbo/plutus/machine.hpp>
#include <turbo/plutus/uplc.hpp>
#include "common.hpp"

namespace turbo::cli::plutus_eval {
    using namespace cardano;
    using namespace turbo::plutus;

    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "plutus-eval";
            cmd.desc = "evaluate a Plutus script and print its result and costs";
            cmd.args.expect({ "<script-path>" });
            cmd.opts.try_emplace("format", "a script format: uplc or flat", "uplc");
            cmd.opts.try_emplace("plutus", "a plutus version: v1, v2, or v3", "v3");
        }

        void run(const arguments &args, const options &opts) const override
        {
            const auto &path = args.at(0);
            const auto &format = opts.at("format").value();
            const auto typ = parse_version(opts.at("plutus").value());
            if (format == "uplc")
                return _eval<uplc::script>(path, typ);
            if (format == "flat")
                return _eval<flat::script>(path, typ);
            throw error(fmt::format("unsupported script format: {}", format));
        }
    private:
        static script_type parse_version(const std::string &version)
        {
            if (version == "v1")
                return script_type::plutus_v1;
            if (version == "v2")
                return script_type::plutus_v2;
            if (version == "v3")
                return script_type::plutus_v3;
            throw error(fmt::format("unsupported plutus version: {}", version));
        }

        template<typename S>
        static void _eval(const std::string &path, const script_type typ)
        {
            allocator alloc {};
            const S script { alloc, file::read(path) };
            machine m { alloc, typ };
            const auto [res, costs] = m.evaluate(script.program());
            logger::info("costs: {}", costs);
            logger::info("result: {}", res);
            logger::info("result hash: {}", crypto::blake2b::digest(flat::encode_cbor(script.version(), res)));
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}