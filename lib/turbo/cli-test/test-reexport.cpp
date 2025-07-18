/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cli/common.hpp>
#include <turbo/cardano/ledger/state.hpp>
#include <turbo/cbor/compare.hpp>

namespace turbo::cli::test_reexport {
    using namespace cardano::ledger;

    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "test-reexport";
            cmd.desc = "reexport node state by importing and exporting it and compare the results";
            cmd.args.expect({ "<node-state-path>" });
        }

        void run(const arguments &args) const override
        {
            const auto &orig_path = args.at(0);
            timer t1 { "load", logger::level::info };
            const auto orig_data = file::read(orig_path);
            t1.stop_and_print();
            timer t2 { "deserialize", logger::level::info };
            state st {};
            const auto tip = st.deserialize_node(orig_data);
            t2.stop_and_print();
            timer t3 { "reserialize", logger::level::info };
            const auto own_data = st.to_cbor(tip).flat();
            t3.stop_and_print();
            const auto diff = cbor::compare(orig_data, own_data);
            logger::info("compare result: {}", diff);
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}