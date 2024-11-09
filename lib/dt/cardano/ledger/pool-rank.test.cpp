/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2024 Alex Sierkov (alex dot sierkov at gmail dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/cardano/ledger/pool-rank.hpp>
#include <dt/test.hpp>

using namespace daedalus_turbo;
using namespace daedalus_turbo::cardano::ledger::pool_rank;

suite cardano_ledger_pool_rank_suite = [] {
    "cardano::ledger::pool_rank"_test = [] {
        "samples"_test = [] {
            expect(samples().size() == 100);
            test_close(samples().at(0), 0.005);
            test_close(samples().at(99), 0.995);
        };
        "leader_probability"_test = [] {
            test_close(1.7347313e-05, leader_probability(0.05, 0.0033822777078451414, 0.9));
        };
        "likelihoods"_test = [] {
            const double k = 0.05;
            const double d = 0.9;
            const double d_2 = 0.8;
            "case 1"_test = [&] {
                // test values are of the mainnet's pool 00000036D515E12E18CD3C88C74F09A67984C2C279A5296AA96EFE89
                // for epochs 212 and 213 respectively
                const auto rel_stake = static_cast<double>(rational { 107345780007031, 31737719158318701 });
                const uint64_t num_blocks = 17;

                const auto lks = likelihoods(num_blocks, 432'000, rel_stake, k, d);
                test_close(0.0F, lks.at(0));
                test_close(18.601470947265625F, lks.at(1));
                test_close(82.56731414794922F, lks.at(99));

                const uint64_t num_blocks_2 = 45;
                const auto rel_stake_2 = static_cast<double>(rational { 124038160325127, 31752906801646541 });
                const auto lks_2 = likelihoods(num_blocks_2, 432'000, rel_stake_2, k, d_2, lks);
                test_close(0.0F, lks_2.at(0));
                test_close(66.00578F, lks_2.at(1));
                test_close(295.3736F, lks_2.at(99));
            };
            "case 2"_test = [&] {
                // test values are of the mainnet's pool D9812F8D30B5DB4B03E5B76CFD242DB9CD2763DA4671ED062BE808A0
                // for epochs 212 and 213 respectively
                const uint64_t num_blocks = 71;
                const auto rel_stake = static_cast<double>(rational { 8435261784377, 857776193468073 });
                const auto lks = likelihoods(num_blocks, 432'000, rel_stake, k, d);
                test_close(0.0F, lks.at(0));
                test_close(77.78363F, lks.at(1));
                test_close(354.26038F, lks.at(99));

                const uint64_t num_blocks_2 = 84;
                const auto rel_stake_2 = static_cast<double>(rational { 229876590276640, 31752906801646541 });
                const auto lks_2 = likelihoods(num_blocks_2, 432'000, rel_stake_2, k, d_2, lks);
                test_close(0.0F, lks_2.at(0));
                test_close(161.96799F, lks_2.at(1));
                test_close(731.71985F, lks_2.at(99));
            };
        };
    };
};