/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/test.hpp>
#include <turbo/storage/partition.hpp>
#include <turbo/storage/test.hpp>

using namespace turbo;
using namespace turbo::storage;

suite storage_partition_suite = [] {
    using boost::ext::ut::v2_1_0::nothrow;
    "storage::partition"_test = [] {
        static std::string data_dir = turbo::storage::sample_registry_path();
        const chunk_registry cr { data_dir, chunk_registry_settings_t { .mode=chunk_registry::mode::store } };
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

        "partition_map epoch bounds"_test = [&] {
            const epoch_partition_map epochs { cr };
            const auto first_epoch = cr.make_slot(epochs.at(0).first_slot()).epoch();
            const auto &last = epochs.at(epochs.size() - 1);
            const auto last_epoch = cr.make_slot(last.last_slot()).epoch();
            const partition_map first { cr, { .last_epoch=first_epoch, .num_parts=1 } };
            expect_equal(first.size(), 1);
            expect_equal(first.at(0).offset(), epochs.at(0).offset());
            expect_equal(first.at(0).end_offset(), epochs.at(0).end_offset());
            const partition_map tail { cr, { .first_epoch=last_epoch, .num_parts=1 } };
            expect_equal(tail.size(), 1);
            expect_equal(tail.at(0).offset(), last.offset());
            expect_equal(tail.at(0).end_offset(), last.end_offset());
            const partition_map all { cr, { .first_epoch=first_epoch, .last_epoch=last_epoch, .num_parts=4 } };
            expect_equal(all.size(), 4);
            uint64_t offset = 0;
            for (const auto &part: all) {
                expect_equal(part.offset(), offset);
                offset = part.end_offset();
            }
            expect_equal(offset, cr.num_bytes());
            const partition_map missing { cr, { .first_epoch=last_epoch + 1, .num_parts=1 } };
            expect_equal(missing.size(), 0);
            expect(throws([&] { partition_map { cr, { .num_parts=0 } }; }));
            expect(throws([&] { partition_map { cr, { .first_epoch=last_epoch + 1, .last_epoch=last_epoch, .num_parts=1 } }; }));
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
