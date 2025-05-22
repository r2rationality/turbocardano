/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <ranges>
#include <dt/common/test.hpp>
#include <dt/cardano.hpp>
#include <dt/cardano/common/cert.hpp>
#include <dt/json.hpp>
#include <dt/zpp.hpp>

using namespace daedalus_turbo;
using namespace daedalus_turbo::cardano;

suite cardano_common_suite = [] {
    "cardano::common"_test = [] {
        "amount"_test = [] {
            {
                cardano::amount a { 1'010 };
                const std::string_view a_text { "0.001010 ADA" };
                expect(fmt::format("{}", a) == a_text);
            }
            {
                cardano::amount a { 678'900'012'345 };
                std::string a_text { "678900.012345 ADA" };
                expect(fmt::format("{}", a) == a_text);
            }
        };
        const auto &cfg = cardano::config::get();
        cfg.shelley_start_epoch(208);
        "slot"_test = [&] {
            expect(cardano::slot { cfg } == 0);

            const cardano::slot byron_1 { 7, cfg };
            expect(byron_1 == 7);
            expect(byron_1.epoch() == 0);
            expect(byron_1.utc_month() == "2017-09") << byron_1.utc_month();
            expect(byron_1.unixtime() == 1506203231) << byron_1.unixtime(); // 1596051751

            const cardano::slot byron_2 { 2'158'515, cfg };
            expect(byron_2 == 2158515);
            expect(byron_2.epoch() == 99);
            expect(byron_2.utc_month() == "2019-02") << byron_2.utc_month();
            expect(byron_2.unixtime() == 1549373391) << byron_2.unixtime();

            const cardano::slot shelley_1 { 8'812'978, cfg };
            expect(shelley_1 == 8'812'978);
            test_same(shelley_1.epoch(), 218ULL);
            expect(shelley_1.utc_month() == "2020-09") << shelley_1.utc_month();
            expect(shelley_1.unixtime() == 1600379269) << shelley_1.unixtime();

            const cardano::slot babbage_1 { 101'142'576, cfg };
            expect(babbage_1 == 101142576ULL);
            expect(babbage_1.epoch() == 431);
            expect(babbage_1.utc_month() == "2023-08") << babbage_1.utc_month();
            expect(babbage_1.unixtime() == 1692708867) << babbage_1.unixtime();

            expect(cardano::slot { 21600, cfg }.epoch() == 1);
            expect(cardano::slot { 2981652, cfg }.epoch() == 138);
            expect(cardano::slot { 4449600, cfg }.epoch() == 206);
            expect(cardano::slot { 4492800, cfg }.epoch() == 208);
            expect(cardano::slot { 75745595, cfg }.epoch() == 372);
            expect(cardano::slot { 75772873, cfg }.epoch() == 373);

            expect(cardano::slot::from_epoch(0, cfg) == 0);
            expect(cardano::slot::from_epoch(208, cfg) == 208 * 21600);
            expect(cardano::slot::from_epoch(213, cfg) == 6652800_u);
            expect(cardano::slot::from_epoch(214, cfg) == 7084800_u);
            expect(cardano::slot::from_epoch(215, cfg) == 7516800_u);

            // convert a timepoint on the 2024-03-22 in unix time to a cardano slot
            const auto st1 = cardano::slot::from_time(std::chrono::system_clock::time_point { std::chrono::seconds { 1'711'093'053 } }, cfg);
            expect(st1 == 119526762_ull);
        };
        "chunk_id"_test = [&] {
            expect(cardano::slot { 4363200, cfg }.chunk_id() == 202_ull);
            expect(cardano::slot { 4492840, cfg }.chunk_id() == 208_ull);
            expect(cardano::slot { 4514400, cfg }.chunk_id() == 209_ull);
            expect(cardano::slot { 4557600, cfg }.chunk_id() == 211_ull);
        };
        "tx_size"_test = [] {
            for (const auto &[sz, exp_sz]: {
                std::pair { 0U, 0U },
                std::pair { 55U, 256U },
                std::pair { 256U, 256U },
                std::pair { 512U, 512U },
                std::pair { 600U, 768U },
                std::pair { 16384U, 16384U },
                std::pair { 16385U, 16640U },
                std::pair { 32768U, 32768U },
                std::pair { 32769U, 33024U },
                std::pair { 65200U, 65280U },
                std::pair { 65280U, 65280U }
            }) {
                expect(cardano::tx_size { sz } == exp_sz) << "tx_size" << sz << "=>" << cardano::tx_size { sz } << "!=" << exp_sz;
            }
            expect(boost::ut::throws<error>([]() { cardano::tx_size { 655281 }; }));
            expect(boost::ut::throws<error>([]() { cardano::tx_size { 65536 }; }));
            expect(boost::ut::throws<error>([]() { cardano::tx_size { 100000 }; }));
        };
        "tx_out_idx"_test = [] {
            for (const auto &[idx, exp_idx]: {
                std::pair { 0U, 0U },
                std::pair { 55U, 55U },
                std::pair { 256U, 256U },
                std::pair { 600U, 600U },
                std::pair { 16384U, 16384U },
                std::pair { 32769U, 32769U },
                std::pair { 65200U, 65200U },
                std::pair { 65535U, 65535U }
            }) {
                expect(cardano::tx_out_idx { idx } == exp_idx) << "tx_out_idx" << idx << "=>" << cardano::tx_out_idx { idx } << "!=" << exp_idx;
            }
            expect(boost::ut::throws<error>([]() { cardano::tx_out_idx { 65536 }; }));
            expect(boost::ut::throws<error>([]() { cardano::tx_out_idx { 100000 }; }));
        };
        "stake_deleg"_test = [] {
            auto chunk = zstd::read("./data/chunk-registry/compressed/chunk/977E9BB3D15A5CFF5C5E48617288C5A731DB654C0B42D63627C690CEADC9E1F3.zstd");
            cbor::zero2::decoder dec { chunk };
            size_t num_delegs = 0;
            while (!dec.done()) {
                auto &block_tuple = dec.read();
                const block_container blk { numeric_cast<uint64_t>(block_tuple.data_begin() - chunk.data()), block_tuple };
                blk->foreach_tx([&](const auto &tx) {
                    tx.foreach_cert([&](const auto &c) {
                        std::visit([&](const auto &cv) {
                            using T = std::decay_t<decltype(cv)>;
                            if constexpr (std::is_same_v<T, stake_deleg_cert>
                                    || std::is_same_v<T, stake_reg_deleg_cert>
                                    || std::is_same_v<T, stake_vote_deleg_cert>
                                    || std::is_same_v<T, stake_vote_reg_deleg_cert>) {
                                ++num_delegs;
                            }
                        }, c.val);
                    });
                });
            }
            test_same(251, num_delegs);
        };
        "pool_reg"_test = [] {
            auto chunk = zstd::read("./data/chunk-registry/compressed/chunk/DF597E3FA352A7BD2F021733804C33729EBAA3DCAA9C0643BD263EFA09497B03.zstd");
            cbor::zero2::decoder dec { chunk };
            size_t num_regs = 0;
            while (!dec.done()) {
                auto &block_tuple = dec.read();
                const block_container blk { numeric_cast<uint64_t>(block_tuple.data_begin() - chunk.data()), block_tuple };
                blk->foreach_tx([&](const auto &tx) {
                    tx.foreach_cert([&](const auto &c) {
                        std::visit([&](const auto &cv) {
                            using T = std::decay_t<decltype(cv)>;
                            if constexpr (std::is_same_v<T, pool_reg_cert>) {
                                ++num_regs;
                            }
                        }, c.val);
                    });
                });
            }
            test_same(2, num_regs);
        };
        "stake_ident"_test = [] {
            stake_ident i1 { cardano::key_hash::from_hex("41fbfce15acccb420982704c9e591f83ab3315c3314a18ecf65346e0"), false };
            stake_ident i2 { cardano::key_hash::from_hex("41fbfce15acccb420982704c9e591f83ab3315c3314a18ecf65346e0"), true };
            expect(i1 < i2);
            expect(i1 == i1);
            expect(i1 != i2);
            const auto j2 = i2.to_json();
            const auto j2_hash = to_lower(daedalus_turbo::json::value_to<std::string>(j2.at("hash")));
            expect(j2_hash == "41fbfce15acccb420982704c9e591f83ab3315c3314a18ecf65346e0") << j2_hash;
            expect(j2.at("script").as_bool());
        };
        "pay_ident"_test = [] {
            pay_ident i1 { cardano::key_hash::from_hex("41fbfce15acccb420982704c9e591f83ab3315c3314a18ecf65346e0") };
            pay_ident i2 { cardano::key_hash::from_hex("41fbfce15acccb420982704c9e591f83ab3315c3314a18ecf65346e0"), pay_ident::ident_type::SHELLEY_SCRIPT };
            pay_ident i3 { cardano::key_hash::from_hex("41fbfce15acccb420982704c9e591f83ab3315c3314a18ecf65346e0"), pay_ident::ident_type::BYRON_KEY };
            expect(i1 < i2);
            expect(i2 < i3);
            expect(i1 == i1);
            expect(i1 != i2);
            expect(i1 != i3);
            const auto j2 = i2.to_json();
            const auto j2_hash = to_lower(daedalus_turbo::json::value_to<std::string>(j2.at("hash")));
            expect(j2_hash == "41fbfce15acccb420982704c9e591f83ab3315c3314a18ecf65346e0") << j2_hash;
            expect(daedalus_turbo::json::value_to<std::string>(j2.at("type")) == "shelley-script");
            expect(daedalus_turbo::json::value_to<std::string>(i1.to_json().at("type")) == "shelley-key");
            expect(daedalus_turbo::json::value_to<std::string>(i3.to_json().at("type")) == "byron-key");
        };
        "param_update"_test = [] {
            param_update u1 {};
            u1.min_fee_a = 200;
            u1.min_fee_b = 1000;
            u1.max_block_body_size = 16384;
            u1.max_transaction_size = 8192;
            u1.max_block_header_size = 2048;
            u1.key_deposit = 500;
            u1.pool_deposit = 500;
            u1.e_max = 3000;
            u1.n_opt = 150;
            u1.pool_pledge_influence = rational_u64 { 5, 7 };
            u1.expansion_rate = rational_u64 { 1, 1000 };
            u1.treasury_growth_rate = rational_u64 { 2, 10 };
            u1.decentralization = rational_u64 { 1, 10 };
            u1.extra_entropy = cardano::nonce {};
            u1.protocol_ver = protocol_version { 5, 2 };
            u1.min_utxo_value = 1'000'000;
            u1.rehash();
            auto u2 = u1;
            u2.rehash();
            expect(u2 == u1);
            expect(daedalus_turbo::zpp::serialize(u1) == daedalus_turbo::zpp::serialize(u2));
            u2.min_utxo_value = 2'000'000;
            u2.rehash();
            expect(!(u2 == u1));
            const auto s1 = fmt::format("{}", u1);
            const auto s2 = fmt::format("{}", u2);
            expect(s1 != s2);
            expect(daedalus_turbo::zpp::serialize(u1) != daedalus_turbo::zpp::serialize(u2));
        };
    };
};
