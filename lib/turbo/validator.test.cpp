/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano.hpp>
#include <turbo/chunk-registry.hpp>
#include <turbo/common/test.hpp>
#include <turbo/sync/mocks.hpp>

namespace {
    using namespace turbo;
    using namespace turbo::sync;
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
            expect_equal(cr.valid_end_offset(), 0);
            const auto ex_ptr = cr.accept_progress({}, chain1.tip, [&] {
                cr.add_file(0, chunk1_path);
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
                cr.add_file(0, chunk1_path);
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
                cr.add_file(0, chunk1_path);
                throw error("some failure, rollback now");
            });
            expect(static_cast<bool>(ex_ptr));
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
                cr.add_file(0, chunk1_path);
            });
            expect(static_cast<bool>(ex_ptr));
            expect(cr.num_blocks() == failure_height) << cr.num_blocks();
            size_t expected_size = 0;
            for (size_t i = 0; i < failure_height; ++i)
                expected_size += chain1.blocks.at(i)->blk.raw().size();
            const auto expected_data = static_cast<buffer>(chain1.data).subbuf(0, expected_size);
            expect_equal(expected_size, cr.valid_end_offset());
            expect(cr.valid_end_offset() < chain1.data.size()) << cr.valid_end_offset();
            expect(!cr.chunks().empty());
            if (!cr.chunks().empty()) {
                const auto &stored_chunk = cr.chunks().begin()->second;
                expect_equal(expected_size, stored_chunk.data_size);
                expect_equal(crypto::blake2b::digest<cardano::block_hash>(expected_data), stored_chunk.data_hash);
                expect(zstd::read(cr.full_path(stored_chunk.rel_path())) == expected_data);
            }
        };
        "snapshot certified core reload"_test = [] {
            const std::string dir { "tmp/validator-snapshot-core" };
            const auto chunk_path = fmt::format("{}/chain.chunk", dir);
            std::filesystem::remove_all(dir);
            const auto chain = gen_chain();
            zstd::write(chunk_path, chain.data);
            uint64_t core_offset = 0;
            {
                chunk_registry cr { dir, chunk_registry::mode::validate, cardano::config { chain.cfg } };
                expect(!cr.accept_progress({}, chain.tip, [&] { cr.add_file(0, chunk_path); }));
                core_offset = cr.chunks().begin()->second.blocks.front().end_offset();
            }
            const auto state_path = fmt::format("{}/validate/state.json", dir);
            auto snapshots = json::load(state_path);
            auto &latest = snapshots.as_array().back().as_object();
            latest.insert_or_assign("trustedAuthorityEpoch", json::value(nullptr));
            latest.insert_or_assign("certifiedCoreOffset", core_offset);
            json::save_pretty(state_path, snapshots);

            chunk_registry restored { dir, chunk_registry::mode::validate, cardano::config { chain.cfg } };
            expect_equal(chain.data.size(), restored.valid_end_offset());
            const auto core = restored.core_tip();
            expect(core && core->end_offset == core_offset);
        };
        "VRF-disabled validation has no certified core"_test = [] {
            const std::string dir { "tmp/validator-no-vrf-core" };
            const auto chunk_path = fmt::format("{}/chain.chunk", dir);
            std::filesystem::remove_all(dir);
            const auto chain = gen_chain();
            zstd::write(chunk_path, chain.data);
            chunk_registry cr { dir, chunk_registry::mode::validate,
                cardano::config { chain.cfg }, scheduler::get(), file_remover::get(), true, false };
            expect(!cr.accept_progress({}, chain.tip, [&] { cr.add_file(0, chunk_path); }));
            expect(!cr.core_tip());
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
                expect_equal(5, e_it->epoch);
                s.erase(e_it);
            } else {
                expect(false);
            }
            if (const auto e_it = s.next_excessive(); e_it != s.end()) {
                expect_equal(200, e_it->epoch);
                s.erase(e_it);
            } else {
                expect(false);
            }
            s.emplace(5, 5 * 10000, 5 * 432000, false);
            s.emplace(200, 200 * 10000, 200 * 432000, false);
            std::set<uint64_t> removed {}, kept {};
            s.remove_excessive([&](const auto &s) { removed.emplace(s.epoch); }, [&](const auto &s) { kept.emplace(s.epoch); });
            expect_equal(std::set<uint64_t> { 5, 200 }, removed);
            expect_equal(std::set<uint64_t> { 20, 250, 450, 518, 519 }, kept);
        };
        "snapshot provenance"_test = [] {
            const validator::snapshot original {
                7, 1234, 567, true, std::optional<uint64_t> { 6 }, 1000,
                validator::snapshot_format_version
            };
            expect_equal(original, validator::snapshot::from_json(original.to_json()));
            const validator::snapshot without_core {
                7, 1234, 567, true, std::nullopt, 0, validator::snapshot_format_version
            };
            expect_equal(without_core, validator::snapshot::from_json(without_core.to_json()));
        };
    };
};
