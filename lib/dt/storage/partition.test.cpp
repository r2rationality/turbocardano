/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/common/test.hpp>
#include <dt/storage/partition.hpp>

using namespace daedalus_turbo;
using namespace daedalus_turbo::storage;

suite storage_partition_suite = [] {
    using boost::ext::ut::v2_1_0::nothrow;
    "storage::partition"_test = [] {
        static std::string data_dir = install_path("./data/chunk-registry");
        const chunk_registry cr { data_dir, chunk_registry::mode::store };
        "partition_map"_test = [&] {
            {
                const partition_map pm { cr };
                test_same(cr.num_chunks(), pm.size());
            }
            {
                const partition_map pm { cr, 4 };
                test_same(4, pm.size());
                expect(nothrow([&] { pm.find(0); }));
                expect(nothrow([&] { pm.find(cr.num_bytes() - 1); }));
                expect(throws([&] { pm.find(cr.num_bytes()); }));
                test_same(0, pm.find_no(0));
                test_same(3, pm.find_no(cr.num_bytes() - 1));
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
            test_same(cr.num_bytes(), num_parsed.load(std::memory_order_relaxed));
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
            test_same(11, num_parsed.load(std::memory_order_relaxed));
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
            test_same(cr.num_bytes(), num_parsed.load(std::memory_order_relaxed));
            test_same(8, num_epochs.load(std::memory_order_relaxed));
        };
    };
};