/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/test.hpp>
#include <turbo/storage/partition.hpp>

using namespace turbo;
using namespace turbo::storage;

suite storage_partition_suite = [] {
    using boost::ext::ut::v2_1_0::nothrow;
    "storage::partition"_test = [] {
        static std::string data_dir = install_path("./data/chunk-registry");
        const chunk_registry cr { data_dir, chunk_registry::mode::store };
        "partition_map"_test = [&] {
            {
                const partition_map pm { cr };
                expect_equal(cr.chunks().size(), pm.size());
            }
            {
                const partition_map pm { cr, 4 };
                expect_equal(4, pm.size());
                expect(nothrow([&] { pm.find(0); }));
                expect(nothrow([&] { pm.find(cr.num_bytes() - 1); }));
                expect(throws([&] { pm.find(cr.num_bytes()); }));
                expect_equal(0, pm.find_no(0));
                expect_equal(3, pm.find_no(cr.num_bytes() - 1));
                for (const auto off: { uint64_t { 0 }, cr.num_bytes() / 2, cr.num_bytes() - 1 }) {
                    const auto &p = pm.find(off);
                    expect(p.offset() <= off) << p.offset() << off;
                    expect(p.end_offset() > off) << p.end_offset() << off;
                }
            }
        };

        "parse_parallel"_test = [&] {
            std::atomic_uint64_t num_parsed { 0 };
            parse_parallel<uint64_t>(cr, 4,
                [&](auto &part, const auto &blk) {
                    part += blk.size();
                },
                [&](const size_t, const auto &) {
                    return uint64_t { 0 };
                },
                [&](auto &&tmp, const size_t, const auto &) {
                    num_parsed.fetch_add(tmp, std::memory_order_relaxed);
                }
            );
            expect_equal(cr.num_bytes(), num_parsed.load(std::memory_order_relaxed));
        };

        "parse_parallel_slot_range"_test = [&] {
            std::atomic_uint64_t num_parsed { 0 };
            parse_parallel_slot_range<uint64_t>(cr, 10, 20,
                [&](auto &part, const auto &) {
                    ++part;
                },
                [&](const size_t, const auto &) {
                    return uint64_t { 0 };
                },
                [&](auto &&tmp, const size_t, const auto &) {
                    num_parsed.fetch_add(tmp, std::memory_order_relaxed);
                }
            );
            expect_equal(11, num_parsed.load(std::memory_order_relaxed));
        };

        "parse_parallel_epoch"_test = [&] {
            std::atomic_uint64_t num_parsed { 0 };
            std::atomic_size_t num_epochs { 0 };
            parse_parallel_epoch<uint64_t>(cr,
                [&](auto &part, const auto &blk) {
                    part += blk.size();
                },
                [&](const size_t, const auto &) {
                    return uint64_t { 0 };
                },
                [&](auto &&tmp, const size_t, const auto &) {
                    num_parsed.fetch_add(tmp, std::memory_order_relaxed);
                    num_epochs.fetch_add(1, std::memory_order_relaxed);
                }
            );
            expect_equal(cr.num_bytes(), num_parsed.load(std::memory_order_relaxed));
            expect_equal(8, num_epochs.load(std::memory_order_relaxed));
        };
    };
};