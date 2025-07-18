/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cardano/common/types.hpp>
#include <turbo/common/test.hpp>
#include <turbo/history.hpp>

using namespace turbo;

suite history_suite = [] {
    "history"_test = [] {
        static const std::string src_dir { "./data/chunk-registry"s };
        static const std::string data_dir { "./tmp" };
        for (const auto &e: std::filesystem::directory_iterator { data_dir }) {
            if (e.is_directory())
                std::filesystem::remove_all(e.path());
        }
        "simple reconstruction"_test = [] {
            chunk_registry src_cr { src_dir, chunk_registry::mode::store };
            chunk_registry idxr { data_dir, chunk_registry::mode::index };
            idxr.import(src_cr);
            
            reconstructor r { idxr };
            const auto &b1 = r.find_block(648087);
            const auto &b2 = r.find_block(648088);
            expect(b1.slot == b2.slot) << b1.slot << " " << b2.slot;
            const auto &m1 = r.find_block(652756);
            const auto &m2 = r.find_block(652756 + 665);
            expect(m1.slot == m2.slot) << m1.slot << " " << m2.slot;
            const auto &e1 = r.find_block(162'930'893);
            const auto &e2 = r.find_block(162'930'893 + 30028);
            expect(e1.slot == e2.slot) << e1.slot << " " << e2.slot;

            // known-item search
            {
                const cardano::address_buf addr_buf { "stake1uxw70wgydj63u4faymujuunnu9w2976pfeh89lnqcw03pksulgcrg" };
                const cardano::address addr { addr_buf };
                const history hist = r.find_history(addr.stake_id());
                expect_equal(2, hist.transactions.size());
                expect_equal(cardano::amount { 32476258673 }, hist.utxo_balance());
            }
            
            // missing-item search
            {
                const history hist = r.find_history(cardano::address { cardano::address_buf { "0xE10001020304050607080910111213141516171819202122232425262728" } }.stake_id());
                expect_equal(0, hist.transactions.size());
            }
        };
    };
};