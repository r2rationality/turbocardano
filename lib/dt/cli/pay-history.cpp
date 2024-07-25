/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2024 Alex Sierkov (alex dot sierkov at gmail dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */
#include <dt/cardano.hpp>
#include <dt/cli.hpp>
#include <dt/history.hpp>

namespace daedalus_turbo::cli::pay_history {
    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "pay-history";
            cmd.desc = "list all transactions referencing a given payment address";
            cmd.args.expect({ "<data-dir>", "<pay-addr>" });
        }

        void run(const arguments &args) const override
        {
            timer t { "reconstruction and serialization", logger::level::debug };
            const auto &data_dir = args.at(0);
            cardano::address_buf addr_raw { args.at(1) };
            if (addr_raw.size() == 28)
                addr_raw.insert(addr_raw.begin(), 0x61);
            chunk_registry cr { data_dir };
            reconstructor r { cr };
            cardano::address addr { addr_raw.span() };
            std::cout << fmt::format("{}", r.find_history(addr.pay_id()));
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}