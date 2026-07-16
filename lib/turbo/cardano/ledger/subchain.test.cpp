/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/config.hpp>
#include <turbo/cardano/ledger/subchain.hpp>
#include <turbo/common/test.hpp>

using namespace turbo;
using namespace turbo::cardano::ledger;

suite cardano_ledger_subchain_suite = [] {
    using boost::ext::ut::v2_1_0::nothrow;
    "cardano::ledger::subchain"_test = [] {
        const auto hash1 = crypto::blake2b::digest<cardano::block_hash>(std::string_view { "1" });
        const auto hash2 = crypto::blake2b::digest<cardano::block_hash>(std::string_view { "2" });
        const auto hash3 = crypto::blake2b::digest<cardano::block_hash>(std::string_view { "3" });
        const auto hash4 = crypto::blake2b::digest<cardano::block_hash>(std::string_view { "4" });
        const auto hash5 = crypto::blake2b::digest<cardano::block_hash>(std::string_view { "5" });
        const auto hash6 = crypto::blake2b::digest<cardano::block_hash>(std::string_view { "6" });
        const auto hash7 = crypto::blake2b::digest<cardano::block_hash>(std::string_view { "7" });
        const auto hash8 = crypto::blake2b::digest<cardano::block_hash>(std::string_view { "8" });
        const auto hash9 = crypto::blake2b::digest<cardano::block_hash>(std::string_view { "9" });
        const auto hash10 = crypto::blake2b::digest<cardano::block_hash>(std::string_view { "10" });
        "check_coherency"_test = [&] {
            expect(nothrow([&] {
                subchain { .offset=0, .num_bytes=100, .num_blocks=1, .valid_blocks=0,
                    .first_block_slot=0, .first_block_hash=hash1, .last_block_slot=0, .last_block_hash=hash1 }.check_coherency();
            }));
            expect(throws([&] {
                subchain { .offset=0, .num_bytes=0, .num_blocks=1, .valid_blocks=0,
                    .first_block_slot=0, .first_block_hash=hash1, .last_block_slot=0, .last_block_hash=hash1 }.check_coherency();
            }));
            expect(throws([&] {
                subchain { .offset=0, .num_bytes=100, .num_blocks=0, .valid_blocks=0,
                    .first_block_slot=0, .first_block_hash=hash1, .last_block_slot=0, .last_block_hash=hash2 }.check_coherency();
            }));
            expect(throws([&] {
                subchain { .offset=0, .num_bytes=100, .num_blocks=2, .valid_blocks=0,
                    .first_block_slot=1, .first_block_hash=hash1, .last_block_slot=0, .last_block_hash=hash2 }.check_coherency();
            }));
            expect(throws([&] {
                subchain { .offset=0, .num_bytes=100, .num_blocks=1, .valid_blocks=0,
                    .first_block_slot=0, .first_block_hash=hash1, .last_block_slot=0, .last_block_hash=hash2 }.check_coherency();
            }));
            expect(throws([&] {
                subchain { .offset=0, .num_bytes=100, .num_blocks=1, .valid_blocks=0,
                    .first_block_slot=0, .first_block_hash=hash1, .last_block_slot=1, .last_block_hash=hash1 }.check_coherency();
            }));
            expect(throws([&] {
                subchain { .offset=0, .num_bytes=100, .num_blocks=2, .valid_blocks=0,
                    .first_block_slot=0, .first_block_hash=hash1, .last_block_slot=1, .last_block_hash=hash1 }.check_coherency();
            }));
        };
        "merge"_test = [&] {
            subchain sc1 { 100, 200, 1, 0, 5, hash1, 5, hash1 };
            expect(nothrow([&]{ sc1.check_coherency(); }));
            expect(!sc1);
            expect(sc1.end_offset() == 300_ull);
            subchain sc2 { 300, 200, 1, 1, 10, hash2, 10, hash2 };
            expect(!!sc2);
            expect(sc1 < sc2);
            sc1.merge(sc2);
            expect_equal(100, sc1.offset);
            expect_equal(400, sc1.num_bytes );
            expect_equal(500, sc1.end_offset());
            expect_equal(2, sc1.num_blocks);
            expect_equal(1, sc1.valid_blocks);
            expect(!sc1);
            expect(throws([&] {
                sc1.merge(subchain { 1000, 100, 1, 0, 10, hash2, 10, hash2 });
            }));
            expect(throws([&] {
                sc1.merge(subchain { 300, 100, 2, 0, 4, hash2, 10, hash2 });
            }));
        };
        "incorrect subchain"_test = [&] {
            subchain_list sl {};
            // invalid slot range
            expect(throws([&] {
                sl.add(subchain { .offset=0, .num_bytes=100, .num_blocks=1, .valid_blocks=1,
                    .first_block_slot=100, .last_block_slot=0 });
            }));
            sl.add(subchain { .offset=0, .num_bytes=100, .num_blocks=2, .valid_blocks=2,
                    .first_block_slot=0, .first_block_hash=hash1, .last_block_slot=100, .last_block_hash=hash2 });
            // intersecting chunks
            expect(throws([&] {
                sl.add(subchain { .offset=99, .num_bytes=100, .num_blocks=2, .valid_blocks=2,
                    .first_block_slot=101, .first_block_hash=hash3, .last_block_slot=200, .last_block_hash=hash4 });
            }));
            // duplicate
            expect(throws([&] {
                sl.add(subchain { .offset=0, .num_bytes=100, .num_blocks=2, .valid_blocks=2,
                    .first_block_slot=0, .first_block_hash=hash1, .last_block_slot=100, .last_block_hash=hash2 });
            }));
        };
        "subchain_list"_test = [&]
        {
            // keep an own instance to prevent the sharing of shelley_start_epoch with other tests
            const cardano::config cfg { configs_dir::get() };
            "merge_valid"_test = [&] {
                subchain_list sl {};
                sl.add(subchain { 200, 200, 2, 2, 1'000, hash3, 1'500, hash4 });
                sl.add(subchain { 1000, 200, 2, 2, 10'000, hash5, 10'500, hash6 });
                expect_equal(0, sl.valid_size());
                expect_equal(cardano::optional_point {}, sl.max_valid_point());
                sl.add(subchain { 0, 200, 2, 2, 0, hash1, 900, hash2 });
                expect_equal( 400, sl.valid_size());
                expect_equal(cardano::optional_point { cardano::point { hash4, 1'500 } }, sl.max_valid_point());
                expect(sl.size() == 2_ull);
            };
            "find"_test = [&] {
                subchain_list sl {};
                sl.add(subchain { 200, 200, 2, 2, 1'000, hash3, 1'500, hash4 });
                sl.add(subchain { 1000, 200, 2, 2, 10'000, hash5, 10'500, hash6 });
                sl.add(subchain { 0, 200, 2, 2, 0, hash1, 900, hash2 });
                expect_equal( 400, sl.valid_size());
                expect(sl.find(300) != sl.end());
                expect(sl.find(0) != sl.end());
                expect(sl.find(1199) != sl.end());
                expect(throws([&] { sl.find(999); }));
                expect(throws([&] { sl.find(1200); }));
            };
            "truncate removes pending tail"_test = [&] {
                subchain_list sl {};
                sl.add(subchain { 0, 200, 2, 2, 0, hash1, 900, hash2 });
                sl.add(subchain { 200, 200, 2, 0, 1'000, hash3, 1'500, hash4 });
                expect_equal(2, sl.size());
                expect(sl.truncate(200));
                expect_equal(1, sl.size());
                expect_equal(200, sl.valid_size());
                expect(throws([&] { sl.find(200); }));
            };
            "merge_same_epoch"_test = [&] {
                subchain_list sl {};
                sl.add(subchain { 200, 200, 2, 2, 21600 * 2, hash1, 21600 * 3 - 1, hash2 });
                sl.add(subchain { 1000, 200, 2, 0, 21600 * 3, hash3, 21600 * 3 + 100, hash4 });
                sl.add(subchain { 1200, 200, 2, 0, 21600 * 3 + 101, hash5, 21600 * 3 + 1000, hash6 });
                sl.add(subchain { 1400, 200, 2, 0, 21600 * 3 + 1001, hash7, 21600 * 3 + 2000, hash8 });
                sl.add(subchain { 1600, 200, 2, 0, 21600 * 4, hash9, 21600 * 5 - 1, hash10 });
                expect_equal(sl.valid_size(), 0);
                expect_equal(sl.size(), 5);
                sl.merge_same_epoch(cfg);
                expect_equal(sl.valid_size(), 0);
                expect_equal(sl.size(), 3);
            };
            "merge_same_epoch partially valid"_test = [&] {
                subchain_list sl {};
                sl.add(subchain { 0, 700, 10, 10, 0, hash1, 21600 * 2 + 10000, hash2 });
                sl.add(subchain { 700, 700, 10, 10, 21600*2 + 10001, hash2, 21600 * 3 + 10000, hash3 });
                expect_equal(1400, sl.valid_size());
                expect_equal(1, sl.size());
                sl.add(subchain { 1400, 200, 2, 0, 21600 * 3 + 10001, hash3, 21600 * 3 + 20000, hash4 });
                expect_equal(2, sl.size());
                sl.add(subchain { 1600, 200, 2, 0, 21600 * 3 + 20001, hash5, 21600 * 4 - 1, hash6 });
                expect_equal(3, sl.size());
                sl.merge_same_epoch(cfg);
                expect_equal(1400, sl.valid_size());
                expect_equal(2, sl.size());
                sl.report_valid_blocks(1500, 2);
                expect_equal(1400, sl.valid_size());
                expect_equal(2, sl.size());
                sl.report_valid_blocks(1700, 2);
                expect_equal(1800, sl.valid_size());
                expect_equal(1, sl.size());
            };
        };
    };
};
