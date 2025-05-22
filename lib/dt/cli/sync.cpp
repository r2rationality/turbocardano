/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/cli.hpp>
#include <dt/sync/p2p.hpp>

namespace daedalus_turbo::cli::sync_p2p {
    using namespace daedalus_turbo::cardano::network;
    using namespace daedalus_turbo::cardano;

    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "sync";
            cmd.desc = "synchronize the blockchain with a peer on the Cardano Network";
            cmd.args.expect({ "<data-dir>" });
            cmd.opts.emplace("max-slot", "do not synchronize beyond this slot");
            cmd.opts.emplace("peer-host", "a Cardano Network host to connect to");
            cmd.opts.try_emplace("peer-port", "a TCP port to use for connecting to a Cardano Network peer", "3001");
            cmd.opts.emplace("version-min", "a minimum Cardano protocol version to request");
            cmd.opts.emplace("version-max", "a maximum Cardano protocol version to request");
        }

        void run(const arguments &args, const options &opts) const override
        {
            const auto &data_dir = args.at(0);
            std::optional<network::address> addr {};
            optional_slot max_slot {};
            version_config_t versions {};
            if (const auto opt_it = opts.find("max-slot"); opt_it != opts.end() && opt_it->second)
                max_slot = std::stoull(*opt_it->second);
            if (const auto opt_it = opts.find("version-min"); opt_it != opts.end() && opt_it->second)
                versions.min = std::stoull(*opt_it->second);
            if (const auto opt_it = opts.find("version-max"); opt_it != opts.end() && opt_it->second)
                versions.max = std::stoull(*opt_it->second);
            if (const auto opt_it = opts.find("peer-host"); opt_it != opts.end() && opt_it->second)
                addr.emplace(*opt_it->second, *opts.at("peer-port"));
            chunk_registry cr { data_dir };
            sync::p2p::syncer syncer { cr };
            const auto peer = syncer.find_peer(addr, versions);
            syncer.sync(peer, max_slot);
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}