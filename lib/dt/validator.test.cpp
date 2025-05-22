/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/common/test.hpp>
#include <dt/cardano.hpp>
#include <dt/sync/mocks.hpp>
#include <dt/chunk-registry.hpp>

namespace {
    using namespace daedalus_turbo;
    using namespace daedalus_turbo::sync;
}

suite validator_suite = [] {
    "validator"_test = [] {
        static std::string data_dir { "tmp/validator" };
        "success"_test = [&] {
            std::filesystem::remove_all(data_dir);
            const std::string chunk1_name { "chunk1.chunk" };
            const auto chunk1_path = fmt::format("{}/{}", data_dir, chunk1_name);
            const auto chain1 = gen_chain();
            zstd::write(chunk1_path, chain1.data);
            chunk_registry cr { data_dir, chunk_registry::mode::validate, cardano::config { chain1.cfg } };
            test_same(cr.valid_end_offset(), 0);
            const auto ex_ptr = cr.accept_progress({}, chain1.tip, [&] {
                cr.add(0, chunk1_path);
            });
            expect(!ex_ptr);
            expect(cr.valid_end_offset() == chain1.data.size()) << cr.valid_end_offset();
        };
        "rollback"_test = [&] {
            std::filesystem::remove_all(data_dir);
            const std::string chunk1_name { "chunk1.chunk" };
            const auto chunk1_path = fmt::format("{}/{}", data_dir, chunk1_name);
            const auto chain1 = gen_chain();
            zstd::write(chunk1_path, chain1.data);
            chunk_registry cr { data_dir, chunk_registry::mode::validate, cardano::config { chain1.cfg } };
            expect(cr.valid_end_offset() == 0_ull);
            const auto ex_ptr = cr.accept_progress({}, chain1.tip, [&] {
                throw error("some failure, rollback now");
                cr.add(0, chunk1_path);
            });
            expect(static_cast<bool>(ex_ptr));
            expect(cr.valid_end_offset() == 0_ull);
        };
        "progress_despite_failure"_test = [&] {
            std::filesystem::remove_all(data_dir);
            const std::string chunk1_name { "chunk1.chunk" };
            const auto chunk1_path = fmt::format("{}/{}", data_dir, chunk1_name);
            const auto chain1 = gen_chain();
            zstd::write(chunk1_path, chain1.data);
            chunk_registry cr { data_dir, chunk_registry::mode::validate, cardano::config { chain1.cfg } };
            expect(cr.valid_end_offset() == 0_ull);
            const auto ex_ptr = cr.accept_progress({}, chain1.tip, [&] {
                cr.add(0, chunk1_path);
                throw error("some failure, rollback now");
            });
            expect(!ex_ptr);
            expect(cr.valid_end_offset() == chain1.data.size()) << cr.valid_end_offset();
        };
        "failure at block 7"_test = [&] {
            static constexpr uint64_t failure_height = 7;
            std::filesystem::remove_all(data_dir);
            const std::string chunk1_name { "chunk1.chunk" };
            const auto chunk1_path = fmt::format("{}/{}", data_dir, chunk1_name);
            const auto chain1 = gen_chain({ .failure_height=failure_height });
            zstd::write(chunk1_path, chain1.data);
            chunk_registry cr { data_dir, chunk_registry::mode::validate, cardano::config { chain1.cfg } };
            expect(cr.valid_end_offset() == 0_ull);
            const auto ex_ptr = cr.accept_progress({}, chain1.tip, [&] {
                cr.add(0, chunk1_path);
            });
            expect(!ex_ptr);
            expect(cr.num_blocks() == failure_height) << cr.num_blocks();
            expect(cr.valid_end_offset() < chain1.data.size()) << cr.valid_end_offset();
        };
        "excessive snapshot"_test = [&] {
            validator::snapshot_set s {};
            expect(s.next_excessive() == s.end());
            s.emplace(5, 5 * 10000, 5 * 432000, false);
            expect(s.next_excessive() == s.end());
            s.emplace(20, 20 * 10000, 20 * 432000, false);
            expect(s.next_excessive() == s.end());
            s.emplace(200, 200 * 10000, 200 * 432000, false);
            expect(s.next_excessive() == s.end());
            s.emplace(250, 250 * 10000, 250 * 432000, false);
            expect(s.next_excessive() == s.end());
            s.emplace(450, 450 * 10000, 450 * 432000, false);
            expect(s.next_excessive() == s.end());
            s.emplace(518, 518 * 10000, 518 * 432000, false);
            expect(s.next_excessive() != s.end());
            s.emplace(519, 519 * 10000, 519 * 432000, false);
            if (const auto e_it = s.next_excessive(); e_it != s.end()) {
                test_same(5, e_it->epoch);
                s.erase(e_it);
            } else {
                expect(false);
            }
            if (const auto e_it = s.next_excessive(); e_it != s.end()) {
                test_same(200, e_it->epoch);
                s.erase(e_it);
            } else {
                expect(false);
            }
            s.emplace(5, 5 * 10000, 5 * 432000, false);
            s.emplace(200, 200 * 10000, 200 * 432000, false);
            std::set<uint64_t> removed {}, kept {};
            s.remove_excessive([&](const auto &s) { removed.emplace(s.epoch); }, [&](const auto &s) { kept.emplace(s.epoch); });
            test_same(std::set<uint64_t> { 5, 200 }, removed);
            test_same(std::set<uint64_t> { 20, 250, 450, 518, 519 }, kept);
        };
    };
};