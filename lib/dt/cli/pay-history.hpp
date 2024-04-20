/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2024 Alex Sierkov (alex dot sierkov at gmail dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */
#ifndef DAEDALUS_TURBO_CLI_PAY_HISTORY_HPP
#define DAEDALUS_TURBO_CLI_PAY_HISTORY_HPP

#include <dt/cardano.hpp>
#include <dt/cli.hpp>
#include <dt/history.hpp>

namespace daedalus_turbo::cli::pay_history {
    struct cmd: command {
        const command_info &info() const override
        {
            static const command_info i { "pay-history", "<data-dir> <pay-addr>", "list all transactions referencing a given payment address" };
            return i;
        }

        void run(const arguments &args) const override
        {
            if (args.size() < 2) _throw_usage();
            timer t { "reconstruction and serialization", logger::level::debug };
            const auto &data_dir = args.at(0);
            cardano::address_buf addr_raw { args.at(1) };
            if (addr_raw.size() == 28) addr_raw.insert(addr_raw.begin(), 0x61);
            indexer::incremental cr { indexer::default_list(data_dir), data_dir };
            reconstructor r { cr };
            cardano::address addr { addr_raw.span() };
            std::cout << fmt::format("{}", r.find_history(addr.pay_id()));
        }
    };
}

#endif // !DAEDALUS_TURBO_CLI_PAY_HISTORY_HPP