/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/asio.hpp>
#include <dt/cli.hpp>
#include <dt/cardano/network/server.hpp>

namespace daedalus_turbo::cli::node_api {
    using namespace daedalus_turbo::cardano::network;

    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "node-api";
            cmd.desc = "start a server providing Cardano Node networking protocol";
            cmd.args.expect({ "<data-dir>" });
            cmd.opts.try_emplace("ip", "an IP address at which to listen for incoming connections", "127.0.0.1");
            cmd.opts.try_emplace("port", "a TCP port at which to listen for incoming connections", "3001");
        }

        void run(const arguments &args, const options &opts) const override
        {
            const auto &data_dir = args.at(0);
            const auto ip = opts.at("ip").value();
            const auto port = opts.at("port").value();
            logger::info("NODE API listens at the address {}:{}", ip, port);
            auto srv = server::make_default(address { ip, port }, data_dir, asio::worker::get(), cardano::config::get());
            srv.run();
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}