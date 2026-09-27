/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano.hpp>
#include <turbo/cardano/ledger/state.hpp>
#include <turbo/cardano/network/chain-source.hpp>
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
        "snapshots count blocks starting at height zero"_test = [] {
            const file::tmp_directory dir { "validator-zero-height-snapshot" };
            const auto chain = gen_chain({ .height=2 });
            const cardano::config cfg { chain.cfg };
            size_t offset = 0;
            for (size_t i = 0; i < chain.blocks.size(); ++i) {
                chunk_registry cr { dir.path(), chunk_registry::mode::validate, cfg };
                expect_equal(cr.num_blocks(), i);
                expect_equal(cr.valid_end_offset(), offset);
                const auto bytes = chain.blocks.at(i)->blk.raw();
                cr.accept_anything_or_throw(cr.tip(), chain.tip, [&] {
                    cr.add_buffer(offset, uint8_vector { bytes });
                });
                offset += bytes.size();
                cr.checkpoint();
                cardano::ledger::state saved { cr.config() };
                expect(fatal(!cr.validator().snapshots().empty()));
                cr.validator().load_snapshot(saved, *cr.validator().snapshots().rbegin());
                const auto subchains = saved.set_subchains({});
                expect(fatal(subchains.size() == 1));
                expect_equal(subchains.begin()->second.num_blocks, i + 1);
                expect_equal(subchains.begin()->second.valid_blocks, i + 1);
            }
            chunk_registry reloaded { dir.path(), chunk_registry::mode::validate, cfg };
            expect_equal(reloaded.num_blocks(), chain.blocks.size());
            expect_equal(reloaded.valid_end_offset(), offset);
        };
        "full, turbo and block imports produce the same state"_test = [] {
            const auto chain = gen_chain({ .height=3 });
            const file::tmp_directory baseline_dir { "validator-shared-baseline" };
            chunk_registry baseline { baseline_dir.path(), chunk_registry::mode::validate,
                chain.cardano_cfg, scheduler::get(), file_remover::get(), true, true, true };
            baseline.validation(validator::validation_mode::none);
            baseline.accept_anything_or_throw({}, chain.tip, [&] { baseline.add_buffer(0, uint8_vector { chain.data }); });
            for (const auto mode: { validator::validation_mode::full, validator::validation_mode::turbo }) {
                const file::tmp_directory dir { "validator-shared-batch" };
                chunk_registry cr { dir.path(), chunk_registry::mode::validate,
                    chain.cardano_cfg, scheduler::get(), file_remover::get(), true, true, true };
                if (mode == validator::validation_mode::turbo) {
                    cr.accept_anything_or_throw({}, chain.tip, [&] {
                        cr.add_buffer(0, uint8_vector { chain.blocks.front()->blk.raw() });
                    });
                    cr.checkpoint();
                }
                cr.validation(mode);
                const auto start_offset = cr.num_bytes();
                cr.accept_anything_or_throw(cr.tip(), chain.tip, [&] {
                    size_t offset = 0;
                    for (const auto &block: chain.blocks) {
                        const auto bytes = block->blk.raw();
                        if (offset >= start_offset)
                            cr.add_buffer(offset, uint8_vector { bytes });
                        offset += bytes.size();
                    }
                });
                expect_equal(cr.tip(), baseline.tip());
                if (mode == validator::validation_mode::turbo) {
                    baseline.checkpoint();
                    cardano::ledger::state expected { chain.cardano_cfg, scheduler::get(), cardano::ledger::state::init_mode::empty };
                    baseline.validator().load_snapshot(expected, *baseline.validator().snapshots().rbegin());
                    expect(cr.validator().state() == expected);
                    expect_equal(cr.validator().snapshots().rbegin()->end_offset, start_offset);
                } else {
                    expect(cr.validator().state() == baseline.validator().state());
                }
            }
            const file::tmp_directory dir { "validator-shared-blocks" };
            chunk_registry cr { dir.path(), chunk_registry::mode::validate,
                chain.cardano_cfg, scheduler::get(), file_remover::get(), true, true, true };
            for (const auto &block: chain.blocks)
                cr.accept_anything_or_throw(cr.tip(), chain.tip, [&] {
                    cr.add_buffer(cr.num_bytes(), uint8_vector { block->blk.raw() });
                });
            expect(cr.validator().state() == baseline.validator().state());
        };
        "witness failure restores the shared state"_test = [] {
            const auto chain = gen_chain({ .height=1 });
            const auto seed = crypto::blake2b::digest<crypto::ed25519::seed>(std::string_view { "1" });
            const auto sk = crypto::ed25519::create_sk_from_seed(seed);
            const auto vk = crypto::ed25519::extract_vk(sk);
            block_producer block { sk, seed, vrf03_create_sk_from_seed(seed) };
            block.prev_hash = chain.cardano_cfg.byron_genesis_hash;
            block.vrf_nonce = chain.cardano_cfg.shelley_genesis_hash;
            block.txs.push_back({ { { {}, 0, sk, vk } }, {} });
            const file::tmp_directory dir { "validator-shared-failure" };
            chunk_registry cr { dir.path(), chunk_registry::mode::validate,
                chain.cardano_cfg, scheduler::get(), file_remover::get(), true, true, true };
            expect(throws([&] {
                cr.accept_anything_or_throw({}, chain.tip, [&] { cr.add_buffer(0, block.cbor()); });
            }));
            expect_equal(cr.validator().state().end_offset(), 0);
            expect(cr.validator().snapshots().empty());
            expect(cr.empty());
            cr.accept_anything_or_throw({}, chain.tip, [&] { cr.add_buffer(0, uint8_vector { chain.data }); });
            expect_equal(cr.valid_end_offset(), chain.data.size());
        };
        "checkpoints recover a newer stored tail in either mode"_test = [] {
            for (const bool continuous: { false, true }) {
                const file::tmp_directory dir { "validator-live-recovery" };
                const auto chain = gen_chain({ .height=3 });
                const cardano::config cfg { chain.cfg };
                file_remover remover;
                cardano::optional_point checkpoint_tip, rollback_tip, final_tip;
                const auto split = chain.blocks.front()->blk.raw().size();
                {
                    chunk_registry cr { dir.path(), chunk_registry::mode::validate, cfg,
                        scheduler::get(), remover, true, true, continuous };
                    cr.accept_anything_or_throw({}, chain.tip, [&] {
                        cr.add_buffer(0, uint8_vector { static_cast<buffer>(chain.data).subbuf(0, split) });
                    });
                    checkpoint_tip = cr.tip();
                    expect(cr.validator().snapshots().empty());
                    cr.checkpoint();
                    cr.accept_anything_or_throw(checkpoint_tip, chain.tip, [&] {
                        cr.add_buffer(split, uint8_vector { static_cast<buffer>(chain.data).subbuf(split) });
                    });
                    cr.before_commit({});
                    final_tip = cr.tip();
                    rollback_tip = cr.find_block_by_slot(chain.blocks.at(1)->blk->slot()).point();
                    expect_equal(cr.validator().snapshots().rbegin()->end_offset, split);
                    // Snapshot boundaries need not remain chunk boundaries.
                    cr.repack();
                }
                // Emulate independently persisted derived metadata ahead of/behind the
                // coordinated checkpoint, and an interrupted checkpoint generation.
                json::save_pretty(dir.path() + "/index/state.json", json::array {});
                std::filesystem::create_directories(dir.path() + "/checkpoints/interrupted");
                {
                    chunk_registry cr { dir.path(), chunk_registry::mode::validate, cfg,
                        scheduler::get(), remover, true, true, continuous };
                    expect_equal(cr.tip(), final_tip);
                    expect_equal(cr.indexer().slices().back().end_offset(), final_tip->end_offset);
                    cr.checkpoint();
                    cr.truncate(rollback_tip);
                    expect_equal(cr.tip(), rollback_tip);
                }
                {
                    chunk_registry cr { dir.path(), chunk_registry::mode::validate, cfg,
                        scheduler::get(), remover, true, true, continuous };
                    expect_equal(cr.tip(), rollback_tip);
                    expect_equal(cr.valid_end_offset(), rollback_tip->end_offset);
                    const auto accepted = cr.tip();
                    cr.before_commit([] { throw error("injected witness failure"); });
                    expect(throws([&] {
                        cr.accept_anything_or_throw(accepted, chain.tip, [&] {
                            cr.add_buffer(accepted->end_offset,
                                uint8_vector { static_cast<buffer>(chain.data).subbuf(accepted->end_offset) });
                        });
                    }));
                    cr.before_commit({});
                    expect_equal(cr.tip(), accepted);
                    cr.recover();
                    expect_equal(cr.tip(), accepted);
                }
            }
        };
        "checkpoints merge closed chunks and preserve pinned views"_test = [] {
            const file::tmp_directory dir { "validator-checkpoint-repack" };
            const auto chain = gen_chain({ .height=1 });
            const auto seed = crypto::blake2b::digest<crypto::ed25519::seed>(std::string_view { "1" });
            block_producer block { crypto::ed25519::create_sk_from_seed(seed), seed, vrf03_create_sk_from_seed(seed) };
            block.prev_hash = chain.cardano_cfg.byron_genesis_hash;
            block.vrf_nonce = chain.cardano_cfg.shelley_genesis_hash;
            file_remover remover;
            auto cr = std::make_shared<chunk_registry>(dir.path(), chunk_registry::mode::validate,
                chain.cardano_cfg, scheduler::get(), remover, true, true, true);
            const auto append = [&](const uint64_t slot) {
                block.slot = slot;
                cr->accept_anything_or_throw(cr->tip(), progress_point { slot }, [&] {
                    cr->add_buffer(cr->num_bytes(), block.cbor());
                });
                block.prev_hash = cr->tip()->hash;
                ++block.height;
            };
            const auto span = cr->config().byron_epoch_length;
            for (const auto slot: { uint64_t { 0 }, uint64_t { 2 }, span })
                append(slot);
            const auto path = cr->full_path(cr->chunks().begin()->second.rel_path());
            std::filesystem::rename(path, path + ".missing");
            expect_equal(cr->repack(chunk_registry::repack_mode_t::merge_closed, 2).chunks_repacked, 0);
            expect(throws([&] { cr->repack(chunk_registry::repack_mode_t::merge_closed, 1); }));
            expect_equal(cr->chunks().size(), 3);
            expect_equal(cr->checkpoint().partial_groups_merged, 0);
            expect_equal(cr->validator().snapshots().rbegin()->end_offset, cr->num_bytes());
            expect_equal(cr->chunks().size(), 3);
            std::filesystem::rename(path + ".missing", path);

            append(2 * span);
            const auto registry_path = dir.path() + "/compressed/state.bin";
            std::filesystem::rename(registry_path, registry_path + ".saved");
            std::filesystem::create_directory(registry_path);
            expect_equal(cr->checkpoint().partial_groups_merged, 0);
            expect_equal(cr->chunks().size(), 4);
            expect_equal(cr->validator().snapshots().rbegin()->end_offset, cr->num_bytes());
            std::filesystem::remove(registry_path);
            std::filesystem::rename(registry_path + ".saved", registry_path);

            append(2 * span + 2);
            cardano::network::chain_source source { cr };
            auto old = source.current();
            expect_equal(cr->checkpoint().partial_groups_merged, 1);
            source.publish(cr->tip());
            const auto current = source.current();
            expect_equal(current->tip, old->tip);
            expect_equal(current->chunks.size(), 4);
            expect_equal(current->chunks.front()->info.num_blocks, 2);
            expect_equal(current->chunks.at(1)->info.data_hash, old->chunks.at(2)->info.data_hash);
            expect_equal(current->chunks.at(1)->info.compression_level, 9);
            expect_equal(current->chunks.at(2)->info.data_hash, old->chunks.at(3)->info.data_hash);
            expect_equal(current->chunks.at(3)->info.data_hash, old->chunks.at(4)->info.data_hash);
            uint8_vector expected;
            expected << *source.read_chunk(*old->chunks.at(0));
            expected << *source.read_chunk(*old->chunks.at(1));
            expect_equal(*source.read_chunk(*current->chunks.front()), expected);
            expect(std::filesystem::exists(path));
            old.reset();
            remover.remove();
            expect(!std::filesystem::exists(path));
            expect_equal(cr->checkpoint().partial_groups_merged, 0);
            expect_equal(cr->repack(chunk_registry::repack_mode_t::merge_closed).chunks_repacked, 0);

            chunk_registry reloaded { dir.path(), chunk_registry::mode::validate, chain.cardano_cfg,
                scheduler::get(), remover, true, true, true };
            expect_equal(reloaded.tip(), cr->tip());
            expect_equal(reloaded.chunks().size(), 4);
            expect_equal(reloaded.valid_end_offset(), cr->num_bytes());
            reloaded.truncate(reloaded.chunks().begin()->second.blocks.front().point());
            expect_equal(reloaded.num_blocks(), 1);
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
                expect(cr.validator().snapshots().empty());
                cr.checkpoint();
            }
            const auto checkpoint = std::filesystem::directory_iterator(dir + "/checkpoints");
            expect(fatal(checkpoint != std::filesystem::directory_iterator {}));
            const auto checkpoint_state_path = (checkpoint->path() / "validate/state.json").string();
            auto snapshots = json::load(checkpoint_state_path);
            expect(fatal(snapshots.as_array().size() == 1));
            auto &latest = snapshots.as_array().back().as_object();
            latest.insert_or_assign("trustedAuthorityEpoch", json::value(nullptr));
            latest.insert_or_assign("certifiedCoreOffset", core_offset);
            json::save_pretty(checkpoint_state_path, snapshots);

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
        "snapshot cadence follows the security window"_test = [] {
            using policy = validator::snapshot_policy;
            using namespace std::chrono_literals;
            expect(!policy::due(100, 103, 90, 2, 9min));
            expect(policy::due(100, 103, 90, 2, 10min));
            expect(policy::due(100, 102, 97, 2, 0s));
            expect(!policy::due(100, 102, 98, 2, 20min));
            expect(!policy::due(100, 100, 100, 2, 20min));
            expect(policy::due(100, {}, 97, 2, 0s));
            expect(!policy::due(2, {}, {}, 2, 20min));
            expect(policy::due(3, {}, {}, 2, 0s));
            expect(!policy::due(100, 102, 97, 2, 9min, true));
            expect(policy::due(100, 102, 97, 2, 10min, true));
            expect(!policy::due(102, 102, 97, 2, 20min, true));
            expect(policy::due(100, 103, 90, 2, 10min, true));
        };
        "two forward checkpoints preserve a rollback anchor"_test = [] {
            const auto initial = gen_chain({ .height=0 });
            auto genesis = initial.cfg.at("shelley-genesis").json();
            genesis["securityParam"] = 2;
            mock_chain_config settings { .height=0 };
            settings.cfg.emplace("shelley-genesis", std::move(genesis));
            const auto chain = gen_chain(settings);
            const auto &cfg = chain.cardano_cfg;
            const auto seed = crypto::blake2b::digest<crypto::ed25519::seed>(std::string_view { "1" });
            for (const bool continuous: { false, true }) {
                const file::tmp_directory dir { continuous ? "validator-two-live" : "validator-two-batch" };
                file_remover remover;
                std::vector<cardano::point> tips;
                {
                    chunk_registry cr { dir.path(), chunk_registry::mode::validate, cfg,
                        scheduler::get(), remover, true, false, continuous };
                    cr.validation(validator::validation_mode::none);
                    block_producer block { crypto::ed25519::create_sk_from_seed(seed), seed, vrf03_create_sk_from_seed(seed) };
                    block.prev_hash = cfg.byron_genesis_hash;
                    block.vrf_nonce = cfg.shelley_genesis_hash;
                    for (size_t height = 0; height < 9; ++height) {
                        block.height = height;
                        block.slot = height * 2;
                        cr.accept_anything_or_throw(cr.tip(), progress_point { block.slot }, [&] {
                            cr.add_buffer(cr.num_bytes(), block.cbor());
                        });
                        tips.emplace_back(*cr.tip());
                        block.prev_hash = cr.tip()->hash;
                        if (height == 0)
                            cr.checkpoint();
                        const auto points = cr.checkpoint_points();
                        expect(fatal(!points.empty()));
                        const auto latest = height / 3 * 3;
                        expect_equal(points.front(), tips.at(latest));
                        expect_equal(points.size(), latest ? 2 : 1);
                        if (latest)
                            expect_equal(points.back(), tips.at(latest - 3));
                        size_t generations = 0;
                        for (const auto &entry: std::filesystem::directory_iterator(dir.path() + "/checkpoints"))
                            if (entry.is_directory()) ++generations;
                        expect_equal(generations, points.size());
                    }
                    cr.checkpoint();
                    const auto points = cr.checkpoint_points();
                    expect(fatal(points.size() == 2));
                    expect_equal(points.front(), tips.at(8));
                    expect_equal(points.back(), tips.at(3));
                    cr.checkpoint();
                    expect_equal(cr.checkpoint_points(), points);
                    size_t files = 0;
                    for (const auto &entry: std::filesystem::directory_iterator(dir.path() + "/validate"))
                        if (entry.path().filename().string().starts_with("ledger-") && entry.path().extension() == ".bin")
                            ++files;
                    expect_equal(files, 2);
                    cr.truncate(tips.at(7));
                    expect_equal(cr.tip(), cardano::optional_point { tips.at(7) });
                    expect_equal(cr.validator().state().end_offset(), tips.at(7).end_offset);
                }
                chunk_registry restored { dir.path(), chunk_registry::mode::validate, cfg,
                    scheduler::get(), remover, true, false, continuous };
                expect_equal(restored.tip(), cardano::optional_point { tips.at(7) });
                restored.checkpoint();
                const auto points = restored.checkpoint_points();
                expect(fatal(points.size() == 2));
                expect_equal(points.front(), tips.at(7));
                expect_equal(points.back(), tips.at(3));
                for (const auto &point: points) {
                    cardano::ledger::state saved { cfg, scheduler::get(), cardano::ledger::state::init_mode::empty };
                    restored.validator().load_snapshot(saved, *restored.validator().snapshots().at_offset(point.end_offset));
                    expect_equal(saved.end_offset(), point.end_offset);
                    expect_equal(saved.valid_end_offset(), point.end_offset);
                }
            }
        };
        "catch-up captures an anchor before the security window"_test = [] {
            const auto initial = gen_chain({ .height=0 });
            auto genesis = initial.cfg.at("shelley-genesis").json();
            genesis["securityParam"] = 2;
            mock_chain_config settings { .height=9 };
            settings.cfg.emplace("shelley-genesis", std::move(genesis));
            const auto chain = gen_chain(settings);
            const file::tmp_directory dir { "validator-window-entry" };
            chunk_registry cr { dir.path(), chunk_registry::mode::validate, chain.cardano_cfg,
                scheduler::get(), file_remover::get(), true, false };
            progress_point target { *chain.tip };
            target.final_checkpoint = true;
            cr.before_commit([&] {
                const auto points = cr.checkpoint_points();
                expect(fatal(points.size() == 1));
                expect_equal(points.front().height, chain.tip->height - cr.config().shelley_security_param - 1);
                expect(!std::filesystem::exists(dir.path() + "/checkpoints"));
            });
            cr.accept_anything_or_throw({}, target, [&] {
                size_t offset = 0;
                for (const auto &block: chain.blocks) {
                    const auto bytes = block->blk.raw();
                    cr.add_buffer(offset, uint8_vector { bytes });
                    offset += bytes.size();
                }
            });
            cr.before_commit({});
            const auto points = cr.checkpoint_points();
            expect(fatal(points.size() == 2));
            expect_equal(points.front(), *cr.tip());
            expect_equal(points.back().height, cr.tip()->height - cr.config().shelley_security_param - 1);
            std::map<std::filesystem::path, std::filesystem::file_time_type> generations;
            for (const auto &entry: std::filesystem::directory_iterator(dir.path() + "/checkpoints"))
                generations.emplace(entry.path(), entry.last_write_time());
            expect_equal(generations.size(), 2);
            cr.checkpoint();
            for (const auto &[path, modified]: generations)
                expect(std::filesystem::exists(path) && std::filesystem::last_write_time(path) == modified);
        };
        "revalidation commits recoverable prefixes without losing unread chunks"_test = [] {
            const auto chain = gen_chain({ .height=0 });
            const auto seed = crypto::blake2b::digest<crypto::ed25519::seed>(std::string_view { "1" });
            for (const size_t interrupt_at: { 0, 1, 2 }) {
                const file::tmp_directory dir { "validator-revalidate-prefix" };
                file_remover remover;
                chunk_registry::chunk_list chunks;
                cardano::optional_point target;
                {
                    chunk_registry stored { dir.path(), chunk_registry::mode::store, chain.cardano_cfg,
                        scheduler::get(), remover };
                    block_producer block { crypto::ed25519::create_sk_from_seed(seed), seed, vrf03_create_sk_from_seed(seed) };
                    block.prev_hash = chain.cardano_cfg.byron_genesis_hash;
                    block.vrf_nonce = chain.cardano_cfg.shelley_genesis_hash;
                    stored.accept_anything_or_throw({}, progress_point { 3 * chain.cardano_cfg.shelley_epoch_length }, [&] {
                        for (size_t height = 0; height < 105; ++height) {
                            block.height = height;
                            block.slot = height / 35 * chain.cardano_cfg.shelley_epoch_length + height % 35 * 2;
                            auto bytes = block.cbor();
                            const parsed_block parsed { bytes, chain.cardano_cfg };
                            block.prev_hash = parsed.blk->hash();
                            stored.add_buffer(stored.num_bytes(), std::move(bytes));
                        }
                    });
                    for (const auto &[offset, chunk]: stored.chunks())
                        chunks.emplace_back(chunk);
                    target = stored.tip();
                }
                expect(fatal(chunks.size() == 105));
                const auto source = dir.path() + "/compressed/revalidate-source.bin";
                {
                    chunk_registry cr { dir.path(), chunk_registry::mode::validate, chain.cardano_cfg,
                        scheduler::get(), remover, false, false };
                    cr.validation(validator::validation_mode::none);
                    size_t commits = 0;
                    cr.before_commit([&] {
                        ++commits;
                        if (commits == interrupt_at)
                            throw error("injected revalidation interruption");
                    });
                    const auto revalidate = [&] { cr.revalidate(chunks, target, std::chrono::seconds { 0 }); };
                    if (interrupt_at) {
                        expect(throws(revalidate));
                        expect_equal(commits, interrupt_at);
                        const auto committed = interrupt_at == 2
                            ? cardano::optional_point { chunks.at(34).blocks.back().point() } : cardano::optional_point {};
                        expect_equal(cr.tip(), committed);
                        expect(std::filesystem::exists(source));
                    } else {
                        revalidate();
                        expect_equal(commits, 3);
                        expect_equal(cr.tip(), target);
                        expect_equal(cr.checkpoint_points().size(), 2);
                        expect(!std::filesystem::exists(source));
                    }
                    cr.before_commit({});
                }
                if (interrupt_at) {
                    remover.remove();
                    for (const auto &chunk: chunks)
                        expect(std::filesystem::exists(dir.path() + "/compressed/" + chunk.rel_path()));
                    {
                        chunk_registry restored { dir.path(), chunk_registry::mode::validate, chain.cardano_cfg,
                            scheduler::get(), remover, false, false };
                        expect_equal(restored.num_bytes(), target->end_offset);
                        expect_equal(restored.validator().state().end_offset(), interrupt_at == 2 ? chunks.at(34).end_offset() : 0);
                    }
                    chunk_registry recovered { dir.path(), chunk_registry::mode::validate, chain.cardano_cfg,
                        scheduler::get(), remover, true, false };
                    expect_equal(recovered.tip(), target);
                    expect_equal(recovered.indexer().slices().back().end_offset(), target->end_offset);
                    expect(!std::filesystem::exists(source));
                }
            }
        };
        "a final checkpoint does not manufacture historical states"_test = [] {
            const auto initial = gen_chain({ .height=0 });
            auto genesis = initial.cfg.at("shelley-genesis").json();
            genesis["securityParam"] = 2;
            mock_chain_config settings { .height=6 };
            settings.cfg.emplace("shelley-genesis", std::move(genesis));
            const auto chain = gen_chain(settings);
            const file::tmp_directory dir { "validator-forward-only" };
            chunk_registry cr { dir.path(), chunk_registry::mode::validate, chain.cardano_cfg,
                scheduler::get(), file_remover::get(), true, false };
            progress_point target { *chain.tip };
            target.height = 100;
            cr.accept_anything_or_throw({}, target, [&] { cr.add_buffer(0, uint8_vector { chain.data }); });
            expect(cr.validator().snapshots().empty());
            const auto tip = cr.tip();
            cr.checkpoint();
            expect_equal(cr.validator().snapshots().size(), 1);
            expect_equal(cr.checkpoint_points().front(), *tip);
            expect_equal(cr.tip(), tip);
            const auto first = cr.chunks().begin()->second.blocks.front().point();
            cr.truncate(first);
            expect_equal(cr.tip(), cardano::optional_point { first });
            expect_equal(cr.validator().state().end_offset(), first.end_offset);
        };
        "checkpoint reuse rejects inconsistent metadata"_test = [] {
            const file::tmp_directory dir { "validator-checkpoint-metadata" };
            const auto chain = gen_chain({ .height=3 });
            chunk_registry cr { dir.path(), chunk_registry::mode::validate, chain.cardano_cfg,
                scheduler::get(), file_remover::get(), true, true, true };
            cr.accept_anything_or_throw({}, chain.tip, [&] { cr.add_buffer(0, uint8_vector { chain.data }); });
            cr.checkpoint();
            const auto newest = cr.tip();
            const auto checkpoint_directory = [&] {
                return std::filesystem::directory_iterator(dir.path() + "/checkpoints")->path();
            };
            const auto original_directory = checkpoint_directory();
            cr.checkpoint();
            expect_equal(checkpoint_directory().string(), original_directory.string());
            for (const std::string_view corruption: { "missing metadata", "ledger boundary", "index boundary", "index gap" }) {
                const auto invalid_directory = checkpoint_directory();
                if (corruption == "missing metadata") {
                    const auto path = (invalid_directory / "manifest.json").string();
                    auto manifest = json::load(path);
                    json::array files;
                    for (const auto &file: manifest.at("files").as_array())
                        if (file.as_string() != "validate/state.json")
                            files.emplace_back(file);
                    manifest.as_object()["files"] = std::move(files);
                    json::save_pretty(path, manifest);
                } else if (corruption == "index boundary" || corruption == "index gap") {
                    const auto path = (invalid_directory / "index/state.json").string();
                    auto state = json::load(path);
                    if (corruption == "index boundary")
                        state.as_array().clear();
                    else
                        state.as_array().front().as_object()["offset"] = 1;
                    json::save_pretty(path, state);
                } else {
                    const auto path = (invalid_directory / "validate/state.json").string();
                    auto state = json::load(path);
                    state.as_array().front().as_object()["endOffset"] = 0;
                    json::save_pretty(path, state);
                }
                cr.checkpoint();
                expect_equal(cr.tip(), newest);
                expect(!std::filesystem::exists(invalid_directory));
                const auto state = json::load((checkpoint_directory() / "validate/state.json").string());
                expect_equal(state.as_array().size(), 1);
                expect_equal(json::value_to<uint64_t>(state.as_array().front().at("endOffset")), newest->end_offset);
            }
        };
        "export selects the newest immutable supported snapshot"_test = [] {
            const auto make_snapshot = [](const uint64_t epoch, const uint64_t offset) {
                return validator::snapshot { epoch, offset, epoch * 100, true };
            };
            auto oldest = make_snapshot(8, 100);
            const auto rollback = make_snapshot(9, 200);
            const auto newest = make_snapshot(10, 300);
            const cardano::optional_point immutable_tip { cardano::point { {}, 1000, 100, 300 } };
            validator::snapshot_set snapshots;
            expect(!snapshots.best_exportable(immutable_tip));
            for (const auto &snap: { oldest, rollback, newest })
                snapshots.emplace(validator::snapshot::from_json(snap.to_json()));
            expect(snapshots.best_exportable(immutable_tip) == snapshots.at_offset(newest.end_offset));
            expect(!snapshots.best_exportable({}));
            auto before_export = immutable_tip;
            before_export->end_offset = oldest.end_offset - 1;
            expect(!snapshots.best_exportable(before_export));
            before_export->end_offset = oldest.end_offset;
            expect(snapshots.best_exportable(before_export) == snapshots.at_offset(oldest.end_offset));

            snapshots.erase(newest);
            expect(snapshots.best_exportable(immutable_tip) == snapshots.at_offset(rollback.end_offset));
            snapshots.erase(oldest);
            oldest.exportable = false;
            snapshots.emplace(oldest);
            expect(snapshots.best_exportable(immutable_tip) == snapshots.at_offset(rollback.end_offset));
            snapshots.erase(rollback);
            expect(!snapshots.best_exportable(immutable_tip));

            snapshots.clear();
            for (const auto &snap: { make_snapshot(8, 100),
                    make_snapshot(9, 200) })
                snapshots.emplace(snap);
            expect(snapshots.best_exportable(immutable_tip) == snapshots.at_offset(200));
            expect(snapshots.best_exportable(before_export) == snapshots.at_offset(100));
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
            auto legacy = original.to_json();
            legacy["role"] = "epoch-end";
            expect_equal(original, validator::snapshot::from_json(legacy));
        };
    };
};
