/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/plutus/flat-encoder.hpp>
#include <turbo/plutus/flat.hpp>
#include <turbo/plutus/machine.hpp>
#include <turbo/plutus/uplc.hpp>
#include "common.hpp"

namespace turbo::cli::plutus_eval {
    using namespace cardano;
    using namespace turbo::plutus;

    struct script_flat_pure: flat::script {
        script_flat_pure(allocator &alloc, uint8_vector &&bytes, const script_type typ,
                const uint64_t protocol_major):
            flat::script{alloc, std::move(bytes), typ, protocol_major, false}
        {
        }
    };

    struct script_flat_cbor: flat::script {
        script_flat_cbor(allocator &alloc, uint8_vector &&bytes, const script_type typ,
                const uint64_t protocol_major):
            flat::script{alloc, std::move(bytes), typ, protocol_major, true}
        {
        }
    };

    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "plutus-eval";
            cmd.desc = "evaluate a Plutus script and print its result and costs";
            cmd.args.expect({ "[<script-path>...]" });
            cmd.opts.try_emplace("format", "a script format: uplc, flat, or flat-cbor", "uplc");
            cmd.opts.try_emplace("plutus", "a plutus version: v1, v2, or v3", "v3");
            cmd.opts.try_emplace("protocol", "a Cardano major protocol version", "11");
        }

        void run(const arguments &args, const options &opts) const override
        {
            const auto &format = opts.at("format").value();
            const auto typ = parse_version(opts.at("plutus").value());
            const auto protocol_major = parse_protocol(opts.at("protocol").value());
            for (const auto &path: args) {
                if (std::filesystem::is_directory(path)) {
                    for (const auto &entry: std::filesystem::directory_iterator(path)) {
                        if (entry.is_regular_file())
                            _eval_file(format, entry.path().string(), typ, protocol_major);
                    }
                } else {
                    _eval_file(format, path, typ, protocol_major);
                }
            }
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

        static uint64_t parse_protocol(const std::string &protocol)
        {
            size_t parsed = 0;
            try {
                const auto major = std::stoull(protocol, &parsed);
                if (parsed == protocol.size())
                    return major;
            } catch (const std::exception &) {
            }
            throw error(fmt::format("unsupported protocol version: {}", protocol));
        }

        static void _eval_file(const std::string &format, const std::string &path, const script_type typ,
                const uint64_t protocol_major) {
            if (format == "uplc")
                return _eval<uplc::script>(path, typ, protocol_major);
            if (format == "flat")
                return _eval<script_flat_pure>(path, typ, protocol_major);
            if (format == "flat-cbor")
                return _eval<script_flat_cbor>(path, typ, protocol_major);
            throw error(fmt::format("unsupported script format: {}", format));
        }

        template<typename S>
        static void _eval(const std::string &path, const script_type typ, const uint64_t protocol_major)
        {
            const timer t{path, logger::level::info};
            try {
                allocator alloc {};
                const S script { alloc, file::read(path), typ, protocol_major };
                machine m { alloc, typ, {}, protocol_major };
                const auto [res, costs] = m.evaluate(script.program());
                logger::info("{} costs: {}", path, costs);
                logger::info("{} result: {}", path, res);
                //logger::info("{} result hash: {}", path, crypto::blake2b::digest(flat::encode_cbor(script.version(), res)));
            } catch (const std::exception &ex) {
                logger::error("{}: {}", path, ex.what());
            }
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}