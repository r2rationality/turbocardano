/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/chunk-registry.hpp>
#include <turbo/common/test.hpp>
#include <turbo/json.hpp>
#include <turbo/sync/mocks.hpp>
#include <turbo/storage/replay.hpp>
#include <turbo/storage/test.hpp>

namespace {
    using namespace turbo;
    using boost::ext::ut::v2_1_0::nothrow;

    auto directory_snapshot(const std::string &directory)
    {
        using file_state_t = std::pair<uint8_vector, std::filesystem::file_time_type>;
        std::map<std::string, file_state_t> files;
        for (const auto &entry: std::filesystem::recursive_directory_iterator(directory)) {
            files.emplace(entry.path().string(), file_state_t {
                entry.is_regular_file() ? file::read(entry.path().string()) : uint8_vector {},
                std::filesystem::last_write_time(entry.path())
            });
        }
        return files;
    }

    uint8_vector successor_bytes(const cardano::config &cfg, const cardano::block_base &previous, const uint64_t slot)
    {
        const auto seed = crypto::blake2b::digest<crypto::ed25519::seed>(std::string_view { "1" });
        cardano::block_producer block { crypto::ed25519::create_sk_from_seed(seed), seed, cardano::vrf03_create_sk_from_seed(seed) };
        block.prev_hash = previous.hash();
        block.height = previous.height() + 1;
        block.slot = slot;
        block.vrf_nonce = cfg.shelley_genesis_hash;
        return block.cbor();
    }

    std::string write_unregistered_chunk(const std::string &data_dir, const buffer bytes)
    {
        const auto hash = crypto::blake2b::digest<cardano::block_hash>(bytes);
        const auto path = data_dir + "/compressed/" + storage::chunk_info::rel_path_from_hash(hash);
        zstd::write(path, bytes);
        return path;
    }
}

suite chunk_registry_suite = [] {
    using boost::ext::ut::v2_1_0::nothrow;
    "replay batch drains pending tasks during producer failure"_test = [] {
        scheduler sched { 1 };
        bool completed = false;
        expect(throws([&] {
            storage::replay_batch_t batch { sched };
            batch.admit(1, 1);
            sched.submit("replay-unwind", 100, [&] { completed = true; });
            throw error("injected producer failure");
        }));
        expect(completed);
    };

    "chunk work memory policy"_test = [] {
        using policy_t = storage::chunk_work_policy_t;
        expect_equal(policy_t::default_budget(0), policy_t::bytes_per_worker);
        expect_equal(policy_t::default_budget(4), 4 * policy_t::bytes_per_worker);
        expect_equal(policy_t::estimated_cost(100, 50), policy_t::fixed_task_overhead + 150);
        expect(policy_t::can_admit(0, 101, 100));
        expect(policy_t::can_admit(40, 60, 100));
        expect(!policy_t::can_admit(40, 61, 100));
        expect(!policy_t::can_admit(1, 101, 100));
        const auto maximum = std::numeric_limits<uint64_t>::max();
        expect_equal(policy_t::estimated_cost(maximum - policy_t::fixed_task_overhead, 0), maximum);
        expect(throws([&] { policy_t::estimated_cost(maximum, 0); }));
        expect(throws([&] { policy_t::estimated_cost(maximum - policy_t::fixed_task_overhead, 1); }));
        expect(throws([] { policy_t::default_budget(std::numeric_limits<size_t>::max()); }));
    };

    "replay batches fill the memory budget before draining"_test = [] {
        using policy_t = storage::chunk_work_policy_t;
        scheduler sched { 1 };
        size_t completed = 0;
        storage::replay_batch_t batch { sched };
        const auto capacity = policy_t::default_budget(sched.num_workers()) / policy_t::estimated_cost(1, 1);
        for (size_t i = 0; i < capacity; ++i) {
            batch.admit(1, 1);
            sched.submit("replay-budget", 100, [&] { ++completed; });
        }
        expect_equal(completed, 0);
        batch.admit(1, 1);
        expect_equal(completed, capacity);
        sched.submit("replay-budget", 100, [&] { ++completed; });
        batch.drain();
        expect_equal(completed, capacity + 1);
    };

    "chunk_registry"_test = [] {
        static std::string data_dir = storage::sample_registry_path();
        static std::string tmp_data_dir = install_path("./tmp/chunk-registry");

        const auto clear_tmp_data_dir = [&] {
            std::filesystem::remove_all(tmp_data_dir);
            std::filesystem::create_directories(tmp_data_dir);
        };

        "every ingestion path accepts valid blocks after rejecting a non-genesis first block"_test = [] {
            const auto chain = sync::gen_chain({ .height=2 });
            enum class ingestion_path_t { raw, compressed, trusted, file };
            for (const auto path: { ingestion_path_t::raw, ingestion_path_t::compressed, ingestion_path_t::trusted, ingestion_path_t::file }) {
                const file::tmp_directory dir { "chunk-registry-ingestion" };
                chunk_registry cr { dir.path(), chunk_registry_settings_t { .ccfg=chain.cardano_cfg } };
                const auto add = [&](const buffer bytes) {
                    if (path == ingestion_path_t::raw) {
                        cr.add_buffer(0, uint8_vector { bytes });
                    } else if (path == ingestion_path_t::compressed) {
                        cr.add_compressed(0, zstd::compress(bytes));
                    } else if (path == ingestion_path_t::trusted) {
                        const auto compressed = zstd::compress(bytes);
                        cr.add_buffer_trusted(0, bytes, compressed);
                    } else {
                        const auto filename = dir.path() + "/input.zstd";
                        zstd::write(filename, bytes);
                        cr.add_file(0, filename);
                    }
                };
                const auto non_genesis_block = chain.blocks.at(1)->blk.raw();
                expect(throws([&] {
                    cr.accept_anything_or_throw({}, *chain.tip, [&] { add(non_genesis_block); });
                }));
                expect(cr.empty());
                expect(!cr.tx());
                expect(nothrow([&] {
                    cr.accept_anything_or_throw({}, *chain.tip, [&] { add(chain.data); });
                }));
                expect_equal(cr.tip(), chain.tip);
            }
        };

        "empty imports leave the destination unchanged"_test = [] {
            const auto chain = sync::gen_chain({ .height=1 });
            const file::tmp_directory source_dir { "chunk-registry-empty-import-source" };
            chunk_registry source { source_dir.path(), chunk_registry_settings_t { .ccfg=chain.cardano_cfg } };
            for (const bool populated: { false, true }) {
                const file::tmp_directory target_dir { "chunk-registry-empty-import-target" };
                chunk_registry target { target_dir.path(), chunk_registry_settings_t { .ccfg=chain.cardano_cfg } };
                if (populated)
                    target.accept_anything_or_throw({}, *chain.tip, [&] { target.add_buffer(0, chain.data); });
                const auto tip = target.tip();
                const auto before = directory_snapshot(target_dir.path());
                bool checked = false;
                target.before_commit([&] { checked = true; });
                expect(nothrow([&] { target.import(source); }));
                expect(!checked);
                expect(!target.tx());
                expect_equal(target.tip(), tip);
                expect(directory_snapshot(target_dir.path()) == before);
            }
        };

        "imports honor commit checks and roll back failures"_test = [] {
            const auto chain = sync::gen_chain({ .height=1 });
            const file::tmp_directory source_dir { "chunk-registry-import-source" };
            const file::tmp_directory target_dir { "chunk-registry-import-target" };
            chunk_registry source { source_dir.path(), chunk_registry_settings_t { .ccfg=chain.cardano_cfg } };
            chunk_registry target { target_dir.path(), chunk_registry_settings_t { .ccfg=chain.cardano_cfg } };
            source.accept_anything_or_throw({}, *chain.tip, [&] { source.add_buffer(0, chain.data); });
            bool checked = false;
            target.before_commit([&] {
                checked = true;
                throw error("injected import commit failure");
            });
            expect(throws([&] { target.import(source); }));
            expect(checked);
            expect(target.empty());
            expect(!target.tx());
        };

        "strict imports count all candidate blocks and reject tied forks"_test = [] {
            const auto chain = sync::gen_chain({ .height=3 });
            const file::tmp_directory dir { "chunk-registry-density" };
            chunk_registry cr { dir.path(), chunk_registry_settings_t { .ccfg=chain.cardano_cfg } };
            const auto two_block_prefix_size = chain.data.size() - chain.blocks.back()->blk.raw().size();
            cr.accept_anything_or_throw({}, *chain.tip, [&] {
                cr.add_buffer(0, uint8_vector { static_cast<buffer>(chain.data).subbuf(0, two_block_prefix_size) });
            });
            expect(nothrow([&] {
                cr.accept_anything_or_throw({}, *chain.tip, [&] { cr.add_buffer(0, chain.data); });
            }));
            const auto saved_tip = cr.tip();
            auto tied_fork = uint8_vector { static_cast<buffer>(chain.data).subbuf(0, two_block_prefix_size) };
            tied_fork << successor_bytes(chain.cardano_cfg, *chain.blocks.at(1)->blk, chain.tip->slot + 1);
            expect(throws([&] {
                cr.accept_anything_or_throw({}, progress_point { chain.tip->slot + 1 }, [&] { cr.add_buffer(0, tied_fork); });
            }));
            expect_equal(cr.tip(), saved_tip);
            expect(!cr.tx());
        };

        "chang"_test = [&] {
            clear_tmp_data_dir();
            std::filesystem::create_directories(tmp_data_dir + "/chang");
            std::filesystem::copy(install_path("./data/chunk-registry/chang"), tmp_data_dir + "/chang");
            const std::string local_path = install_path(tmp_data_dir + "/chang/9326B83719AEAB06A671EA653EE297F1DA601A4FC279A759503D79F55DA6EEC7.zstd");
            const auto bytes = zstd::read(local_path);
            storage::block_reader_t reader { bytes, 0, cardano::config::get() };
            auto indexers = indexer::default_list(tmp_data_dir);
            indexer::chunk_indexer_list_t writers;
            for (const auto &[name, indexer]: indexers) {
                writers.emplace_back(indexer->make_chunk_indexer("update", 0));
            }
            uint64_t end_offset = 0;
            while (!reader.done()) {
                const auto block = reader.next();
                writers.index_block(block);
                end_offset = block.end_offset();
            }
            expect_equal(end_offset, 10746832);
        };

        const auto recreate_tmp_data_dir = [&] {
            clear_tmp_data_dir();
            std::filesystem::copy(data_dir, tmp_data_dir, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
        };

        "startup recovers the contiguous stored prefix"_test = [&] {
            recreate_tmp_data_dir();
            file_remover fr {};
            expect(nothrow([&] {
                chunk_registry cr {
                    tmp_data_dir, chunk_registry_settings_t { .fr=fr }
                };
                expect(!cr.empty());
                expect_equal(cr.chunks().size(), 1);
                expect_equal(cr.num_bytes(), 14'788'562);
                expect_equal(cr.valid_end_offset(), cr.num_bytes());
                expect_equal(cr.max_end_offset(), cr.num_bytes());
                const cardano::optional_point expected_tip { cardano::point {
                    cardano::block_hash::from_hex("3BD04916B6BC2AD849D519CFAE4FFE3B1A1660C098DBCD3E884073DD54BC8911"),
                    21'599, 21'599, 14'788'562
                } };
                expect_equal(cr.tip(), expected_tip);
            }));
            recreate_tmp_data_dir();
            expect(nothrow([&] { chunk_registry cr { tmp_data_dir, chunk_registry_settings_t { .mode=chunk_registry::mode::store } }; }));
        };

        "explicit download recovery replays parallel batches at chunk boundaries"_test = [] {
            scheduler sched { 4 };
            using policy_t = storage::chunk_work_policy_t;
            const auto chain = sync::gen_chain({
                .height=policy_t::default_budget(sched.num_workers()) / policy_t::fixed_task_overhead + 2
            });
            const file::tmp_directory dir { "chunk-registry-download-recovery" };
            file_remover remover;
            const chunk_registry_settings_t settings { .ccfg=chain.cardano_cfg, .fr=remover, .sched=sched, .recover_orphans=true };
            {
                chunk_registry cr { dir.path(), settings };
                cr.accept_anything_or_throw({}, *chain.tip, [&] {
                    cr.add_buffer(0, uint8_vector { chain.blocks.front()->blk.raw() });
                });
                cr.checkpoint();
            }
            for (size_t i = 1; i < chain.blocks.size(); ++i) {
                write_unregistered_chunk(dir.path(), chain.blocks[i]->blk.raw());
            }
            {
                chunk_registry cr { dir.path(), settings };
                expect_equal(cr.valid_end_offset(), chain.data.size());
                expect_equal(cr.tip(), chain.tip);
                expect(fatal(!cr.indexer().slices().empty()));
                expect_equal(cr.indexer().slices().back().end_offset(), cr.num_bytes());
                expect_equal(std::distance(std::filesystem::directory_iterator(dir.path() + "/compressed/chunk"),
                    std::filesystem::directory_iterator {}), cr.chunks().size());
            }
            chunk_registry restored { dir.path(), settings };
            expect_equal(restored.valid_end_offset(), chain.data.size());
        };

        "committed truncation survives leftover files and lost removal marks"_test = [] {
            const auto chain = sync::gen_chain({ .height=2 });
            for (const bool genesis: { false, true }) {
                const file::tmp_directory dir { "chunk-registry-truncate-reopen" };
                file_remover remover;
                std::shared_ptr<void> pin;
                cardano::optional_point expected;
                std::string suffix;
                {
                    chunk_registry cr { dir.path(), chunk_registry_settings_t { .ccfg=chain.cardano_cfg, .fr=remover } };
                    const auto first = chain.blocks.front()->blk.raw();
                    cr.accept_anything_or_throw({}, *chain.tip, [&] {
                        cr.add_buffer(0, uint8_vector { first });
                        cr.add_buffer(first.size(), uint8_vector { chain.blocks.back()->blk.raw() });
                    });
                    suffix = cr.full_path(cr.chunks().rbegin()->second.rel_path());
                    pin = remover.pin(suffix);
                    if (!genesis)
                        expected = cr.chunks().begin()->second.blocks.back().point();
                    cr.truncate(expected);
                    remover.remove();
                    expect(std::filesystem::exists(suffix));
                }
                pin.reset();
                // A new process has no record of the old removal marks.
                file_remover reopened_remover;
                chunk_registry reopened { dir.path(), chunk_registry_settings_t { .ccfg=chain.cardano_cfg, .fr=reopened_remover } };
                expect_equal(reopened.tip(), expected);
                expect(!std::filesystem::exists(suffix));
                expect_equal(reopened.valid_end_offset(), reopened.num_bytes());
            }
        };

        "revalidation retires its source before processor finalization"_test = [] {
            const auto chain = sync::gen_chain({ .height=2 });
            const file::tmp_directory dir { "chunk-registry-revalidate-commit" };
            const auto source = std::filesystem::path { dir.path() } / "compressed/revalidate-source.bin";
            cardano::optional_point prefix;
            {
                chunk_registry cr { dir.path(), chunk_registry_settings_t { .ccfg=chain.cardano_cfg } };
                const auto first = chain.blocks.front()->blk.raw();
                cr.accept_anything_or_throw({}, *chain.tip, [&] {
                    cr.add_buffer(0, uint8_vector { first });
                    cr.add_buffer(first.size(), uint8_vector { chain.blocks.back()->blk.raw() });
                });
                prefix = cr.chunks().begin()->second.blocks.back().point();
                bool finalized = false;
                const chunk_processor failing_processor {
                    .commit_tx=[&] {
                        finalized = true;
                        expect(!std::filesystem::exists(source));
                        throw error("injected finalization failure after revalidation publication");
                    }
                };
                cr.register_processor(failing_processor);
                expect(throws([&] { cr.revalidate(prefix); }));
                cr.remove_processor(failing_processor);
                expect(finalized);
                expect(!std::filesystem::exists(source));
                expect(throws([&] { cr.maintenance(); }));
            }
            chunk_registry reopened { dir.path(), chunk_registry_settings_t { .ccfg=chain.cardano_cfg } };
            expect_equal(reopened.tip(), prefix);
            expect(!std::filesystem::exists(source));
        };

        "pending revalidation commit retires its source before startup restoration"_test = [] {
            const auto chain = sync::gen_chain({ .height=2 });
            const file::tmp_directory dir { "chunk-registry-revalidate-pending" };
            const std::filesystem::path root { dir.path() };
            const auto source = root / "compressed/revalidate-source.bin";
            const auto backup = root / "saved-revalidation-source";
            cardano::optional_point prefix;
            {
                chunk_registry cr { dir.path(), chunk_registry_settings_t { .ccfg=chain.cardano_cfg } };
                const auto first = chain.blocks.front()->blk.raw();
                cr.accept_anything_or_throw({}, *chain.tip, [&] {
                    cr.add_buffer(0, uint8_vector { first });
                    cr.add_buffer(first.size(), uint8_vector { chain.blocks.back()->blk.raw() });
                });
                prefix = cr.chunks().begin()->second.blocks.back().point();
                cr.before_commit([&] {
                    // Obstruct removal after preparation. Installs succeed, but
                    // the pending journal must survive until the source is restored.
                    std::filesystem::rename(source, backup);
                    std::filesystem::create_directory(source);
                });
                expect(throws([&] { cr.revalidate(prefix); }));
                const auto saved = turbo::zpp::load<storage::chunk_map>((root / "compressed/state.bin").string());
                expect(fatal(!saved.empty()));
                expect_equal(saved.rbegin()->second.blocks.back().point(), *prefix);
                expect(throws([&] { cr.maintenance(); }));
            }
            std::filesystem::remove(source);
            std::filesystem::rename(backup, source);
            chunk_registry reopened { dir.path(), chunk_registry_settings_t { .ccfg=chain.cardano_cfg } };
            expect_equal(reopened.tip(), prefix);
            expect(!std::filesystem::exists(source));
            expect(std::filesystem::is_empty(root / "transactions"));
        };

        "unregistered chunks require validation before other registry modes"_test = [] {
            const auto chain = sync::gen_chain({ .height=1 });
            for (const auto mode: { chunk_registry::mode::store, chunk_registry::mode::index }) {
                const file::tmp_directory dir { "chunk-registry-mandatory-maintenance" };
                const auto download = write_unregistered_chunk(dir.path(), chain.data);
                expect(throws([&] {
                    chunk_registry rejected { dir.path(), chunk_registry_settings_t { .mode=mode, .ccfg=chain.cardano_cfg } };
                }));
                expect(std::filesystem::exists(download));
                {
                    chunk_registry repaired { dir.path(), chunk_registry_settings_t { .ccfg=chain.cardano_cfg } };
                    expect_equal(repaired.tip(), chain.tip);
                }
                chunk_registry cr { dir.path(), chunk_registry_settings_t { .mode=mode, .ccfg=chain.cardano_cfg } };
                expect_equal(cr.tip(), chain.tip);
                expect_equal(cr.valid_end_offset(), cr.max_end_offset());
            }
        };

        "validating opens and maintenance preserve clean registry state"_test = [] {
            const auto chain = sync::gen_chain({ .height=1 });
            const file::tmp_directory dir { "chunk-registry-clean-state" };
            file_remover remover;
            const chunk_registry_settings_t settings { .ccfg=chain.cardano_cfg, .fr=remover };
            std::string chunk_path;
            {
                chunk_registry cr { dir.path(), settings };
                cr.accept_anything_or_throw({}, *chain.tip, [&] { cr.add_buffer(0, chain.data); });
                cr.checkpoint();
                chunk_path = cr.full_path(cr.chunks().begin()->second.rel_path());
            }
            const auto state_path = std::filesystem::canonical(dir.path() + "/compressed/state.bin").string();
            std::filesystem::last_write_time(state_path,
                std::filesystem::file_time_type::clock::now() - std::chrono::hours { 24 });
            const auto saved_time = std::filesystem::last_write_time(state_path);
            const auto saved_bytes = file::read(state_path);
            remover.mark(chunk_path);
            remover.mark(state_path);
            chunk_registry cr { dir.path(), settings };
            expect(std::filesystem::last_write_time(state_path) == saved_time);
            expect_equal(file::read(state_path), saved_bytes);
            expect_equal(remover.size(), 0);
            remover.mark(chunk_path);
            remover.mark(state_path);
            cr.maintenance();
            expect(std::filesystem::last_write_time(state_path) == saved_time);
            expect_equal(file::read(state_path), saved_bytes);
            expect_equal(remover.size(), 0);
            expect_equal(zstd::read(chunk_path), chain.data);
        };

        "store and index opens require repair without modifying files"_test = [] {
            const auto chain = sync::gen_chain({ .height=2 });
            enum class damage_t { temporary, orphan, missing_chunk, compression_metadata, index_gap, index_file, index_update,
                index_boundary, epoch_delta, outflow, pending_state, ledger_metadata, ledger_slot, deletion_mark,
                unused_index, index_deletion_mark, snapshot_deletion_mark, temporary_registry, temporary_index, restore_state };
            const std::pair<damage_t, const char *> cases[] {
                { damage_t::temporary, "temporary chunk" },
                { damage_t::orphan, "orphan chunk" },
                { damage_t::missing_chunk, "missing chunk" },
                { damage_t::compression_metadata, "compression metadata" },
                { damage_t::index_gap, "index gap" },
                { damage_t::index_file, "missing index file" },
                { damage_t::index_update, "unfinished ledger index" },
                { damage_t::index_boundary, "index boundary" },
                { damage_t::epoch_delta, "epoch delta" },
                { damage_t::outflow, "outflow" },
                { damage_t::pending_state, "pending state" },
                { damage_t::ledger_metadata, "ledger metadata" },
                { damage_t::ledger_slot, "ledger slot" },
                { damage_t::deletion_mark, "chunk deletion mark" },
                { damage_t::unused_index, "unused index slice" },
                { damage_t::index_deletion_mark, "index deletion mark" },
                { damage_t::snapshot_deletion_mark, "snapshot deletion mark" },
                { damage_t::temporary_registry, "temporary registry metadata" },
                { damage_t::temporary_index, "temporary index metadata" },
                { damage_t::restore_state, "unfinished metadata restoration" }
            };
            for (const auto mode: { chunk_registry::mode::store, chunk_registry::mode::index }) {
                for (const auto &[damage, name]: cases) {
                    const auto context = fmt::format("{}: {}", mode == chunk_registry::mode::store ? "store" : "index", name);
                    const file::tmp_directory dir { "chunk-registry-open-policy" };
                    file_remover remover;
                    const chunk_registry_settings_t writer { .ccfg=chain.cardano_cfg, .fr=remover };
                    const chunk_registry_settings_t reader { .mode=mode, .ccfg=chain.cardano_cfg, .fr=remover };
                    std::string chunk_path;
                    std::string index_path;
                    std::string snapshot_path;
                    std::string unnecessary_path;
                    {
                        chunk_registry cr { dir.path(), writer };
                        cr.accept_anything_or_throw({}, *chain.tip, [&] { cr.add_buffer(0, chain.data); });
                        cr.checkpoint();
                        chunk_path = cr.full_path(cr.chunks().begin()->second.rel_path());
                        index_path = cr.indexer().reader_paths("tx").front();
                        snapshot_path = (std::filesystem::canonical(dir.path() + "/validate")
                            / fmt::format("ledger-{:013}.bin", cr.validator().snapshots().rbegin()->end_offset)).string();
                    }
                    std::filesystem::remove_all(dir.path() + "/checkpoints");
                    const auto state_path = dir.path() + "/compressed/state.bin";
                    switch (damage) {
                        case damage_t::temporary:
                            file::write(chunk_path + ".tmp", std::string_view { "unfinished" });
                            break;
                        case damage_t::orphan:
                            write_unregistered_chunk(dir.path(), chain.blocks.back()->blk.raw());
                            break;
                        case damage_t::missing_chunk:
                            std::filesystem::remove(chunk_path);
                            break;
                        case damage_t::compression_metadata: {
                            auto chunks = turbo::zpp::load<storage::chunk_map>(state_path);
                            ++chunks.begin()->second.compressed_size;
                            turbo::zpp::save(state_path, chunks);
                            break;
                        }
                        case damage_t::index_gap:
                            json::save_pretty(dir.path() + "/index/state.json", json::array {});
                            break;
                        case damage_t::index_file:
                            std::filesystem::remove(index_path);
                            break;
                        case damage_t::index_update:
                            std::filesystem::create_directories(dir.path() + "/index/utxo");
                            file::write(dir.path() + "/index/utxo/index-update-0.data.bin", std::string_view { "unfinished" });
                            break;
                        case damage_t::index_boundary: {
                            const auto path = dir.path() + "/index/state.json";
                            auto state = json::load(path);
                            state.as_array().back().as_object()["maxSlot"] = chain.tip->slot + 1;
                            json::save_pretty(path, state);
                            break;
                        }
                        case damage_t::epoch_delta:
                        case damage_t::outflow: {
                            const auto path = dir.path() + "/index/" + (damage == damage_t::epoch_delta ? "epoch-delta" : "outflow");
                            std::filesystem::create_directories(path);
                            file::write(path + "/unfinished.bin", std::string_view { "unfinished" });
                            break;
                        }
                        case damage_t::pending_state:
                            file::write(dir.path() + "/compressed/state-pre.bin", std::string_view { "unfinished" });
                            break;
                        case damage_t::ledger_metadata:
                            json::save_pretty(dir.path() + "/validate/state.json", json::array {
                                validator::snapshot { 0, chain.data.size() + 1, chain.tip->slot, false }.to_json()
                            });
                            break;
                        case damage_t::ledger_slot: {
                            const auto path = dir.path() + "/validate/state.json";
                            auto state = json::load(path);
                            state.as_array().back().as_object()["lastSlot"] = chain.tip->slot + 1;
                            json::save_pretty(path, state);
                            break;
                        }
                        case damage_t::unused_index:
                            unnecessary_path = dir.path() + "/index/tx/index-slice-unused.data";
                            std::filesystem::copy_file(index_path, unnecessary_path);
                            break;
                        case damage_t::index_deletion_mark:
                            remover.mark(index_path);
                            break;
                        case damage_t::snapshot_deletion_mark:
                            remover.mark(snapshot_path);
                            break;
                        case damage_t::temporary_registry:
                            unnecessary_path = state_path + ".tmp";
                            file::write(unnecessary_path, std::string_view { "unfinished" });
                            break;
                        case damage_t::temporary_index:
                            unnecessary_path = dir.path() + "/index/state.json.tmp";
                            file::write(unnecessary_path, std::string_view { "unfinished" });
                            break;
                        case damage_t::restore_state:
                            unnecessary_path = state_path + ".restore";
                            file::write(unnecessary_path, std::string_view { "unfinished" });
                            break;
                        case damage_t::deletion_mark:
                            remover.mark(chunk_path);
                            break;
                    }
                    const auto before = directory_snapshot(dir.path());
                    const auto marks = remover.removable();
                    expect(throws([&] { chunk_registry rejected { dir.path(), reader }; })) << context;
                    expect(directory_snapshot(dir.path()) == before) << context;
                    expect(remover.removable() == marks) << context;
                    {
                        chunk_registry repaired { dir.path(), writer };
                        expect_equal(repaired.valid_end_offset(), repaired.num_bytes(), context);
                        expect_equal(repaired.num_bytes(), damage == damage_t::missing_chunk ? 0 : chain.data.size(), context);
                    }
                    if (!unnecessary_path.empty()) {
                        expect(!std::filesystem::exists(unnecessary_path)) << context;
                    }
                    expect_equal(remover.size(), 0, context);
                    const auto clean = directory_snapshot(dir.path());
                    chunk_registry cr { dir.path(), reader };
                    cr.maintenance();
                    expect(directory_snapshot(dir.path()) == clean) << context;
                }
            }
        };

        "store and index accept an older ledger snapshot"_test = [] {
            const auto chain = sync::gen_chain({ .height=2 });
            const file::tmp_directory dir { "chunk-registry-older-snapshot" };
            {
                chunk_registry cr { dir.path(), chunk_registry_settings_t { .ccfg=chain.cardano_cfg } };
                const auto first = chain.blocks.front()->blk.raw();
                cr.accept_anything_or_throw({}, progress_point { chain.blocks.front()->blk->slot(), first.size() }, [&] {
                    cr.add_buffer(0, uint8_vector { first });
                });
                const auto checkpoints = cr.data_dir() / "checkpoints";
                expect(!std::filesystem::exists(checkpoints));
                cr.checkpoint();
                expect(std::filesystem::is_directory(checkpoints));
                expect(!std::filesystem::is_empty(checkpoints));
                cr.accept_anything_or_throw(cr.tip(), *chain.tip, [&] {
                    cr.add_buffer(first.size(), uint8_vector { chain.blocks.back()->blk.raw() });
                });
                expect(fatal(!cr.validator().snapshots().empty()));
                expect(cr.validator().snapshots().rbegin()->end_offset < cr.num_bytes());
            }
            const auto before = directory_snapshot(dir.path());
            for (const auto mode: { chunk_registry::mode::store, chunk_registry::mode::index }) {
                chunk_registry cr { dir.path(), chunk_registry_settings_t { .mode=mode, .ccfg=chain.cardano_cfg } };
                expect_equal(cr.tip(), chain.tip);
            }
            expect(directory_snapshot(dir.path()) == before);
        };

        "store and index require an existing registry and reject chain changes"_test = [] {
            for (const auto mode: { chunk_registry::mode::store, chunk_registry::mode::index }) {
                const file::tmp_directory dir { "chunk-registry-existing-only" };
                const auto path = dir.path() + "/registry";
                const chunk_registry_settings_t settings { .mode=mode };
                expect(throws([&] { chunk_registry missing { path, settings }; }));
                expect(!std::filesystem::exists(path));
                {
                    chunk_registry created { path };
                }
                const auto before = directory_snapshot(path);
                chunk_registry cr { path, settings };
                bool action_called = false;
                expect(throws([&] {
                    cr.accept_anything_or_throw({}, progress_point { 0 }, [&] { action_called = true; });
                }));
                expect(!action_called);
                expect(throws([&] { cr.truncate({}); }));
                cr.maintenance();
                expect(directory_snapshot(path) == before);
            }
        };

        "chunk scan preserves sibling files"_test = [] {
            const file::tmp_directory dir { "chunk-registry-scan-scope" };
            const auto db_dir = std::filesystem::path { dir.path() } / "compressed";
            const auto chunk_dir = db_dir / "chunk";
            std::filesystem::create_directories(chunk_dir);
            std::filesystem::create_directories(db_dir / "repack");
            const auto sibling = db_dir / "repack" / "unrelated.zstd";
            const auto temporary = db_dir / "unrelated.tmp";
            file::write(sibling, uint8_vector {});
            file::write(temporary, uint8_vector {});
            {
                chunk_registry cr { dir.path() };
                expect(cr.empty());
            }
            expect(std::filesystem::exists(sibling));
            expect(std::filesystem::exists(temporary));
        };

        "chunk scan rejects unexpected file extensions"_test = [] {
            const file::tmp_directory dir { "chunk-registry-unexpected-extension" };
            const auto chunk_dir = std::filesystem::path { dir.path() } / "compressed/chunk";
            std::filesystem::create_directories(chunk_dir);
            const auto unexpected = chunk_dir / "unexpected.bin";
            file::write(unexpected, uint8_vector {});
            expect(throws([&] {
                chunk_registry cr { dir.path() };
            }));
            expect(std::filesystem::exists(unexpected));
        };

        "chunk scan rejects nested directories"_test = [] {
            const file::tmp_directory dir { "chunk-registry-nested-directory" };
            const auto chunk_dir = std::filesystem::path { dir.path() } / "compressed/chunk";
            std::filesystem::create_directories(chunk_dir / "nested");
            expect(throws([&] {
                chunk_registry cr { dir.path() };
            }));
        };

        "invalid orphan witnesses do not prevent reopening"_test = [] {
            const auto chain = sync::gen_chain({ .height=1 });
            const auto seed = crypto::blake2b::digest<crypto::ed25519::seed>(std::string_view { "1" });
            const auto sk = crypto::ed25519::create_sk_from_seed(seed);
            const auto vk = crypto::ed25519::extract_vk(sk);
            for (const bool existing_prefix: { false, true }) {
                const file::tmp_directory dir { "chunk-registry-invalid-orphan" };
                file_remover remover;
                cardano::optional_point saved_tip;
                {
                    chunk_registry cr { dir.path(), chunk_registry_settings_t { .ccfg=chain.cardano_cfg, .fr=remover, .recover_orphans=true } };
                    if (existing_prefix) {
                        cr.accept_anything_or_throw({}, *chain.tip, [&] { cr.add_buffer(0, chain.data); });
                        saved_tip = cr.tip();
                    }
                }
                cardano::block_producer block_with_missing_input { sk, seed, cardano::vrf03_create_sk_from_seed(seed) };
                block_with_missing_input.prev_hash = saved_tip ? saved_tip->hash : chain.cardano_cfg.byron_genesis_hash;
                block_with_missing_input.slot = saved_tip ? saved_tip->slot + 1 : 0;
                block_with_missing_input.height = saved_tip ? saved_tip->height + 1 : 0;
                block_with_missing_input.vrf_nonce = chain.cardano_cfg.shelley_genesis_hash;
                block_with_missing_input.txs.push_back({ { { {}, 0, sk, vk } }, {} });
                const auto path = write_unregistered_chunk(dir.path(), block_with_missing_input.cbor());
                expect(nothrow([&] {
                    chunk_registry cr { dir.path(), chunk_registry_settings_t { .ccfg=chain.cardano_cfg, .fr=remover, .recover_orphans=true } };
                    expect_equal(cr.tip(), saved_tip);
                    expect_equal(cr.valid_end_offset(), cr.max_end_offset());
                }));
                expect(!std::filesystem::exists(path));
            }
        };

        "duplicate orphan blocks discard all downloads and preserve registered state"_test = [] {
            const auto chain = sync::gen_chain({ .height=3 });
            for (const bool existing_prefix: { false, true }) {
                const file::tmp_directory dir { "chunk-registry-orphan-duplicates" };
                const chunk_registry_settings_t settings { .ccfg=chain.cardano_cfg, .recover_orphans=true };
                cardano::optional_point saved_tip;
                const auto first = chain.blocks.front()->blk.raw();
                if (existing_prefix) {
                    chunk_registry cr { dir.path(), settings };
                    cr.accept_anything_or_throw({}, *chain.tip, [&] { cr.add_buffer(0, uint8_vector { first }); });
                    cr.checkpoint();
                    saved_tip = cr.tip();
                } else {
                    write_unregistered_chunk(dir.path(), first);
                }
                const auto overlap = write_unregistered_chunk(dir.path(), static_cast<buffer>(chain.data).subbuf(first.size()));
                const auto duplicate = write_unregistered_chunk(dir.path(), chain.blocks.back()->blk.raw());
                chunk_registry cr { dir.path(), settings };
                expect_equal(cr.tip(), saved_tip);
                expect(!std::filesystem::exists(overlap));
                expect(!std::filesystem::exists(duplicate));
                expect_equal(std::distance(std::filesystem::directory_iterator(dir.path() + "/compressed/chunk"),
                    std::filesystem::directory_iterator {}), cr.chunks().size());
            }
        };

        "startup discards a chunk overlapping the registered tip"_test = [] {
            const auto chain = sync::gen_chain({ .height=3 });
            const file::tmp_directory dir { "chunk-registry-orphan-registered-overlap" };
            const chunk_registry_settings_t settings { .ccfg=chain.cardano_cfg, .recover_orphans=true };
            cardano::optional_point saved_tip;
            {
                chunk_registry cr { dir.path(), settings };
                const auto prefix_size = chain.data.size() - chain.blocks.back()->blk.raw().size();
                cr.accept_anything_or_throw({}, *chain.tip, [&] {
                    cr.add_buffer(0, uint8_vector { static_cast<buffer>(chain.data).subbuf(0, prefix_size) });
                });
                cr.checkpoint();
                saved_tip = cr.tip();
            }
            const auto path = write_unregistered_chunk(dir.path(),
                static_cast<buffer>(chain.data).subbuf(chain.blocks.front()->blk.raw().size()));
            chunk_registry cr { dir.path(), settings };
            expect_equal(cr.tip(), saved_tip);
            expect(!std::filesystem::exists(path));
        };

        "orphan selection preserves input on unrelated commit failures"_test = [] {
            const auto chain = sync::gen_chain({ .height=1 });
            const file::tmp_directory dir { "chunk-registry-orphan-commit-failure" };
            file_remover remover;
            chunk_registry cr { dir.path(), chunk_registry_settings_t { .ccfg=chain.cardano_cfg, .fr=remover } };
            const auto path = cr.full_path(storage::chunk_info::rel_path_from_hash(chain.data_hash));
            zstd::write(path, chain.data);
            size_t checks = 0;
            cr.before_commit([&] {
                ++checks;
                throw error("injected unrelated commit failure");
            });
            expect(throws([&] { cr.maintenance(true); }));
            expect_equal(checks, 1);
            expect(cr.empty());
            remover.remove();
            expect(std::filesystem::exists(path));
            cr.before_commit({});
            cr.maintenance(true);
            expect_equal(cr.valid_end_offset(), chain.data.size());
            expect_equal(cr.tip(), chain.tip);
        };

        "startup removes a disconnected chunk"_test = [] {
            const auto chain = sync::gen_chain({ .height=2 });
            const file::tmp_directory dir { "chunk-registry-disconnected" };
            const auto path = write_unregistered_chunk(dir.path(), chain.blocks.back()->blk.raw());
            chunk_registry cr { dir.path(), chunk_registry_settings_t { .ccfg=chain.cardano_cfg } };
            expect(cr.empty());
            expect(!std::filesystem::exists(path));
        };

        "startup removes a malformed zstd frame"_test = [] {
            const file::tmp_directory dir { "chunk-registry-malformed-zstd" };
            const auto path = dir.path() + "/compressed/chunk/" + std::string(64, '0') + ".zstd";
            file::write(path, uint8_vector { 0 });
            chunk_registry cr { dir.path() };
            expect(cr.empty());
            expect(!std::filesystem::exists(path));
        };

        "startup removes a chunk with a mismatched content address"_test = [] {
            const auto chain = sync::gen_chain({ .height=1 });
            const file::tmp_directory dir { "chunk-registry-content-address" };
            const auto path = dir.path() + "/compressed/chunk/" + std::string(64, '0') + ".zstd";
            zstd::write(path, chain.data);
            chunk_registry cr { dir.path(), chunk_registry_settings_t { .ccfg=chain.cardano_cfg } };
            expect(cr.empty());
            expect(!std::filesystem::exists(path));
        };

        "startup keeps the first boundary continuation regardless of file creation order"_test = [] {
            const auto chain = sync::gen_chain({ .height=3 });
            const auto alternative = successor_bytes(chain.cardano_cfg, *chain.blocks.front()->blk, chain.tip->slot + 1);
            const auto first = chain.blocks.front()->blk.raw();
            const auto continuation = static_cast<buffer>(chain.data).subbuf(first.size());
            for (const bool fork_first: { false, true }) {
                const file::tmp_directory dir { "chunk-registry-orphan-boundary-fork" };
                const auto branch_path = write_unregistered_chunk(dir.path(), fork_first ? buffer { alternative } : continuation);
                write_unregistered_chunk(dir.path(), first);
                const auto last_path = write_unregistered_chunk(dir.path(), fork_first ? continuation : buffer { alternative });
                chunk_registry cr { dir.path(), chunk_registry_settings_t { .ccfg=chain.cardano_cfg } };
                expect_equal(cr.tip(), chain.tip);
                expect_equal(cr.valid_end_offset(), chain.data.size());
                expect(!std::filesystem::exists(fork_first ? branch_path : last_path));
            }
        };

        "startup discards a branch attached inside a recovered chunk"_test = [] {
            const auto chain = sync::gen_chain({ .height=3 });
            const file::tmp_directory dir { "chunk-registry-internal-fork" };
            write_unregistered_chunk(dir.path(), chain.data);
            const auto fork = successor_bytes(chain.cardano_cfg, *chain.blocks.at(1)->blk, chain.tip->slot + 1);
            const auto path = write_unregistered_chunk(dir.path(), fork);
            chunk_registry cr { dir.path(), chunk_registry_settings_t { .ccfg=chain.cardano_cfg } };
            expect_equal(cr.tip(), chain.tip);
            expect(!std::filesystem::exists(path));
        };

        "startup discards a chunk with a broken internal hash chain"_test = [] {
            const auto chain = sync::gen_chain({ .height=3 });
            const file::tmp_directory dir { "chunk-registry-broken-orphan" };
            uint8_vector bytes;
            bytes << chain.blocks.front()->blk.raw() << chain.blocks.back()->blk.raw();
            const auto path = write_unregistered_chunk(dir.path(), bytes);
            chunk_registry cr { dir.path(), chunk_registry_settings_t { .ccfg=chain.cardano_cfg } };
            expect(cr.empty());
            expect(!std::filesystem::exists(path));
        };

        "startup discards successors with regressing or equal Shelley slots"_test = [] {
            const auto chain = sync::gen_chain({ .height=2 });
            expect(fatal(chain.tip->slot > 0));
            for (const auto slot: { uint64_t { 0 }, chain.tip->slot }) {
                const file::tmp_directory dir { "chunk-registry-invalid-successor-slot" };
                {
                    chunk_registry cr { dir.path(), chunk_registry_settings_t { .ccfg=chain.cardano_cfg } };
                    cr.accept_anything_or_throw({}, *chain.tip, [&] { cr.add_buffer(0, chain.data); });
                }
                const auto successor = successor_bytes(chain.cardano_cfg, *chain.blocks.back()->blk, slot);
                const auto path = write_unregistered_chunk(dir.path(), successor);
                chunk_registry cr { dir.path(), chunk_registry_settings_t { .ccfg=chain.cardano_cfg, .recover_orphans=true } };
                expect_equal(cr.tip(), chain.tip);
                expect(!std::filesystem::exists(path));
            }
        };

        "empty"_test = [&] {
            std::filesystem::remove_all(tmp_data_dir);
            chunk_registry cr { tmp_data_dir };
            expect(cr.num_bytes() == 0_ull);
            expect(cr.num_compressed_bytes() == 0_ull);
            expect(cr.max_slot() == 0_ull);
            expect(cr.empty());
            expect(!cr.has_epoch(0));
            expect(cr.epochs().empty());
            expect(&cr.sched() == &scheduler::get());
            expect(&cr.remover() == &file_remover::get());
            expect(cr.data_dir() == tmp_data_dir);
            expect(!cr.tx());
        };

        // access
        {
            chunk_registry cr { data_dir, chunk_registry_settings_t { .mode=chunk_registry::mode::store } };
            "create chunk registry"_test = [&cr] {
                expect(!cr.empty());
                expect(cr.num_bytes() == 175'115'499_u) << cr.num_bytes();
            };
            "find"_test = [&cr] {
                expect(!cr.empty());
                const auto rel_path = cr.find_offset(100'000'000).rel_path();
                expect_equal(rel_path, "chunk/47F62675C9B0161211B9261B7BB1CF801EDD4B9C0728D9A6C7A910A1581EED41.zstd");
                expect(throws([&cr] { cr.find_offset(200'000'000); }));
                expect(cr.find_block_by_offset(100'000'000).offset == 99'936'542_ull);
                expect(throws([&cr] { cr.find_block_by_offset(200'000'000); }));
                expect(cr.find_last_block_hash(cardano::block_hash::from_hex("EF282E85A8EF8A9C31D255C736F52AA0D52BEA260276BF2FB4AF3ADB700D0F1B")).offset == 84'430'954_ull);
                expect(throws([&cr] { cr.find_last_block_hash(cardano::block_hash {}); }));
                expect(cr.find_block_by_offset(100'000'000).slot == 71420546ULL) << fmt::format("{}", cr.find_block_by_offset(100'000'000).slot);
                expect(throws([&cr] { cr.find_block_by_offset(200'000'000); }));
                expect(cr.find_block_by_slot(71431152).offset == 120'772'796_ull);
                expect(throws([&cr] { cr.find_block_by_slot(72'000'000); }));
                expect(cr.find_offset_it(100'000'000)->second.offset == 84'430'954_ull);
                expect(throws([&cr] { cr.find_offset_it(200'000'000); }));
            };
            "latest_block_after_slot"_test = [&] {
                if (const auto blk = cr.latest_block_after_or_at_slot(0); blk != cr.cend()) {
                    expect_equal(0, blk->slot);
                } else {
                    expect(false);
                }
                if (const auto blk = cr.latest_block_after_or_at_slot(cr.max_slot()); blk != cr.cend()) {
                    expect_equal(cr.max_slot(), blk->slot );
                } else {
                    expect(false);
                }
                const uint64_t mid_slot = cr.max_slot() / 2;
                if (const auto blk = cr.latest_block_after_or_at_slot(mid_slot); blk != cr.cend()) {
                    expect(blk->slot >= mid_slot) << fmt::format("{}", blk->point()) << mid_slot;
                } else {
                    expect(false);
                }
            };
            "latest_block_before_slot"_test = [&] {
                if (const auto blk = cr.latest_block_before_or_at_slot(0); blk != cr.cend()) {
                    expect_equal(0, blk->slot);
                } else {
                    expect(false);
                }
                if (const auto blk = cr.latest_block_before_or_at_slot(cr.max_slot()); blk != cr.cend()) {
                    expect(blk->slot <= cr.max_slot()) << fmt::format("{}", blk->point()) << cr.max_slot();
                    expect(cr.max_slot() - blk->slot < 400) << fmt::format("{}", blk->point()) << cr.max_slot();
                } else {
                    expect(false);
                }
                const uint64_t mid_slot = cr.max_slot() / 2;
                if (const auto blk = cr.latest_block_before_or_at_slot(mid_slot); blk != cr.cend()) {
                    expect(blk->slot <= mid_slot) << fmt::format("{}", blk->point()) << mid_slot;
                    // test chain is sparse so the closeness transition is expected to fail
                    // expect(cr.max_slot() - blk->slot < 400) << fmt::format("{}", blk->point()) << cr.max_slot();
                } else {
                    expect(false);
                }
            };
            "read"_test = [&cr] {
                auto block_tuple_pv = cr.read(28'762'567);
                auto &block_tuple = block_tuple_pv.get();
                expect_equal(cbor::major_type::array, block_tuple.type());
                expect_equal(false, block_tuple.indefinite());
                expect_equal(2, block_tuple.special_uint());
            };
        }
        {
            chunk_registry cr { tmp_data_dir, chunk_registry_settings_t { .mode=chunk_registry::mode::store } };
            "full_path"_test = [&] {
                auto exp = std::filesystem::weakly_canonical(std::filesystem::absolute(tmp_data_dir) / "compressed/some-dir/some-file.ext");
                auto act = cr.full_path("some-dir/some-file.ext");
                expect(exp == act) << act;
                auto dir_path = cr.full_path("some-dir");
                expect(!std::filesystem::exists(dir_path));
                expect(throws([&] { cr.full_path("../../../../../etc/passwd"); }));
                expect(throws([&] { cr.full_path("../index/state.json"); }));
                expect(throws([&] { cr.full_path(cr.data_dir() / "compressed/state.bin"); }));
            };
            "rel_path"_test = [&] {
                auto full_path = std::filesystem::weakly_canonical(std::filesystem::absolute(tmp_data_dir) / "compressed/some-dir/some-file.ext");
                auto exp = std::filesystem::path { "some-dir/some-file.ext" }.make_preferred().string();
                auto act = cr.rel_path(full_path);
                expect(exp == act) << act;
                expect(throws([&] { cr.rel_path(std::filesystem::weakly_canonical("./data2/another-file.txt")); }));
            };
        }

        "truncate at chunk and block boundaries"_test = [] {
            const auto chain = sync::gen_chain({ .height=3 });
            for (const bool split: { false, true }) {
                const file::tmp_directory dir { "chunk-registry-truncate" };
                chunk_registry cr { dir.path(), chunk_registry_settings_t { .ccfg=chain.cardano_cfg } };
                cr.accept_anything_or_throw({}, *chain.tip, [&] {
                    if (split) {
                        for (const auto &block: chain.blocks) {
                            cr.add_buffer(cr.num_bytes(), uint8_vector { block->blk.raw() });
                        }
                    } else {
                        cr.add_buffer(0, chain.data);
                    }
                });
                const auto original = cr.tip();
                cr.truncate(original);
                expect_equal(cr.tip(), original);
                const auto prefix = cr.find_block_by_slot(chain.blocks.at(1)->blk->slot()).point();
                cr.truncate(prefix);
                expect_equal(cr.tip(), cardano::optional_point { prefix });
                expect_equal(cr.num_blocks(), 2);
                cr.truncate({});
                expect(cr.empty());
                expect_equal(cr.valid_end_offset(), 0);
            }
        };

        "live repacking counts fragments per logical chunk including the open chunk"_test = [] {
            const auto chain = sync::gen_chain({ .height=0 });
            const file::tmp_directory dir { "chunk-registry-repack-fragments" };
            storage::chunk_fixture_t fixture { dir.path(), chain.cardano_cfg };
            const auto seed = crypto::blake2b::digest<crypto::ed25519::seed>(std::string_view { "1" });
            cardano::block_producer block {
                crypto::ed25519::create_sk_from_seed(seed), seed, cardano::vrf03_create_sk_from_seed(seed)
            };
            block.prev_hash = chain.cardano_cfg.byron_genesis_hash;
            uint8_vector expected;
            // A global fragment threshold would wrongly merge the three-file group too.
            const std::array<size_t, 2> counts { 3, 4 };
            for (size_t group = 0; group < counts.size(); ++group) {
                for (size_t i = 0; i < counts[group]; ++i) {
                    block.slot = group * chain.cardano_cfg.byron_epoch_length + 2 * i;
                    const auto raw = block.cbor();
                    fixture.add(expected.size(), raw);
                    auto parsed = cbor::zero2::parse(raw);
                    block.prev_hash = cardano::block_container { expected.size(), parsed.get(), chain.cardano_cfg }->hash();
                    expected << raw;
                    ++block.height;
                }
            }
            fixture.save();
            chunk_registry cr { dir.path(), chunk_registry_settings_t { .mode=chunk_registry::mode::store, .ccfg=chain.cardano_cfg } };
            const auto tip = cr.tip();
            expect_equal(cr.repack(chunk_registry::repack_mode_t::merge_fragmented, 3).partial_groups_merged, 1);
            expect_equal(cr.chunks().size(), 4);
            expect_equal(cr.chunks().rbegin()->second.num_blocks, 4);
            expect_equal(cr.chunks().rbegin()->second.compression_level, 3);
            expect_equal(cr.tip(), tip);
            uint8_vector actual;
            for (const auto &[offset, chunk]: cr.chunks())
                actual << zstd::read(cr.full_path(chunk.rel_path()));
            expect_equal(actual, expected);
            expect_equal(cr.repack(chunk_registry::repack_mode_t::merge_fragmented, 3).partial_groups_merged, 0);
        };

        "old chunks bypass the recent fragmentation threshold"_test = [] {
            auto genesis = json::load("./etc/mainnet/shelley-genesis.json").as_object();
            genesis["securityParam"] = 2;
            sync::mock_chain_config settings { .height=0 };
            settings.cfg.emplace("shelley-genesis", std::move(genesis));
            const auto chain = sync::gen_chain(settings);
            for (const bool closed: { false, true }) {
                const file::tmp_directory dir { "chunk-registry-repack-age" };
                storage::chunk_fixture_t fixture { dir.path(), chain.cardano_cfg };
                const auto seed = crypto::blake2b::digest<crypto::ed25519::seed>(std::string_view { "1" });
                cardano::block_producer block {
                    crypto::ed25519::create_sk_from_seed(seed), seed, cardano::vrf03_create_sk_from_seed(seed)
                };
                block.prev_hash = chain.cardano_cfg.byron_genesis_hash;
                uint64_t offset = 0;
                const auto span = chain.cardano_cfg.byron_epoch_length;
                const std::array<uint64_t, 5> slots { 0, 2, span, span + 2, 2 * span };
                for (size_t i = 0; i < (closed ? 5 : 4); ++i) {
                    block.slot = slots[i];
                    const auto raw = block.cbor();
                    fixture.add(offset, raw);
                    auto parsed = cbor::zero2::parse(raw);
                    block.prev_hash = cardano::block_container { offset, parsed.get(), chain.cardano_cfg }->hash();
                    offset += raw.size();
                    ++block.height;
                }
                fixture.save();
                chunk_registry cr { dir.path(), chunk_registry_settings_t { .mode=chunk_registry::mode::store, .ccfg=chain.cardano_cfg } };
                expect_equal(cr.repack(chunk_registry::repack_mode_t::merge_closed, 2).partial_groups_merged, closed ? 1 : 0);
                expect_equal(cr.chunks().size(), 4);
                if (closed) {
                    expect_equal(cr.chunks().begin()->second.num_blocks, 2);
                    expect_equal(cr.repack(chunk_registry::repack_mode_t::merge_closed, 2).partial_groups_merged, 0);
                    expect_equal(cr.repack(chunk_registry::repack_mode_t::merge_closed, 1).partial_groups_merged, 1);
                    expect_equal(cr.chunks().size(), 3);
                }
            }
        };

        "epoch notifications wait for contiguous chunks and flush the final epoch"_test = [] {
            const auto chain = sync::gen_chain({ .height=1 });
            const auto first = chain.blocks.front()->blk.raw();
            const auto next_slot = chain.cardano_cfg.shelley_epoch_length;
            const auto second = successor_bytes(chain.cardano_cfg, *chain.blocks.front()->blk, next_slot);
            const file::tmp_directory dir { "chunk-registry-epoch-notifications" };
            std::vector<uint64_t> epochs;
            uint64_t notified_bytes = 0;
            chunk_processor observer {
                .on_epoch_update = [&](const auto epoch, const auto &info) {
                    epochs.emplace_back(epoch);
                    notified_bytes += info.size();
                }
            };
            chunk_registry cr { dir.path(), chunk_registry_settings_t { .validate_vrf=false, .ccfg=chain.cardano_cfg } };
            cr.register_processor(observer);
            cr.accept_anything_or_throw({}, progress_point { next_slot + 1 }, [&] {
                cr.add_buffer(first.size(), second);
                expect(epochs.empty());
                cr.add_buffer(0, uint8_vector { first });
                expect(epochs == std::vector<uint64_t> { 0 });
                expect_equal(notified_bytes, first.size());
            });
            expect(epochs == std::vector<uint64_t> { 0, 1 });
            expect_equal(notified_bytes, first.size() + second.size());
            expect_equal(cr.num_bytes(), notified_bytes);
        };

        "repack publishes raw files before committing only metadata"_test = [] {
            const auto chain = sync::gen_chain({ .height=2 });
            const file::tmp_directory dir { "chunk-registry-repack-pending" };
            const std::filesystem::path root { dir.path() };
            storage::chunk_fixture_t fixture { dir.path(), chain.cardano_cfg };
            uint64_t offset = 0;
            for (const auto &block: chain.blocks) {
                const auto raw = block->blk.raw();
                fixture.add(offset, raw);
                offset += raw.size();
            }
            fixture.save();
            {
                chunk_registry cr { dir.path(), chunk_registry_settings_t { .mode=chunk_registry::mode::store, .ccfg=chain.cardano_cfg } };
                std::vector<std::string> originals;
                for (const auto &[end, chunk]: cr.chunks())
                    originals.emplace_back(cr.full_path(chunk.rel_path()));
                // Fail the metadata rename after workers have published their outputs.
                std::filesystem::rename(root / "compressed/state.bin", root / "old-state.bin");
                std::filesystem::create_directory(root / "compressed/state.bin");
                expect(throws([&] { cr.repack(); }));
                for (const auto &path: originals)
                    expect(std::filesystem::is_regular_file(path));
                size_t pending = 0;
                for (const auto &entry: std::filesystem::directory_iterator(root / "transactions")) {
                    if (!entry.path().filename().string().starts_with("pending-"))
                        continue;
                    ++pending;
                    const auto manifest = json::load((entry.path() / "manifest.json").string());
                    const auto &actions = manifest.at("install").as_array();
                    expect_equal(actions.size(), 1);
                    expect_equal(json::value_to<std::string>(actions.front().at("path")), std::string { "compressed/state.bin" });
                    expect(std::filesystem::is_empty(entry.path() / "files/scratch"));
                }
                expect_equal(pending, 1);
                expect(throws([&] { cr.repack(); }));
            }
            std::filesystem::remove(root / "compressed/state.bin");
            std::filesystem::rename(root / "old-state.bin", root / "compressed/state.bin");
            chunk_registry recovered { dir.path(), chunk_registry_settings_t { .ccfg=chain.cardano_cfg } };
            expect_equal(recovered.tip(), chain.tip);
            expect(std::filesystem::is_empty(root / "transactions"));
        };

        "epoch-level repacking preserves chunk metadata"_test = [&] {
            clear_tmp_data_dir();
            const std::string src_dir { "./data/chunk-registry-new" };
            const auto j_chunks = turbo::json::load(src_dir + "/epoch-merge.json").as_array();
            storage::chunk_fixture_t fixture { tmp_data_dir };
            for (const auto &j_chunk: j_chunks) {
                const auto source = src_dir + "/" + json::value_to<std::string>(j_chunk.at("relPath"));
                fixture.add(json::value_to<uint64_t>(j_chunk.at("offset")), file::read(source));
            }
            fixture.save();
            chunk_registry cr { tmp_data_dir, chunk_registry_settings_t { .mode=chunk_registry::mode::store } };
            expect_equal(cr.num_bytes(), 2602139);
            expect_equal(cr.epochs().size(), 4);

            expect_equal(7, cr.chunks().size());
            const auto before_repack_size = cr.num_bytes();
            const auto before_repack_blocks = cr.num_blocks();
            chunk_registry::file_set old_chunk_paths {};
            for (const auto &[last_byte_offset, chunk]: cr.chunks())
                old_chunk_paths.emplace(cr.full_path(chunk.rel_path()));
            const auto old_repack_dir = std::filesystem::path { tmp_data_dir } / "compressed" / ".repack-old";
            std::filesystem::create_directories(old_repack_dir);
            const auto repack_stats = cr.repack();
            expect_equal(7, repack_stats.chunks_analyzed);
            expect_equal(4, repack_stats.chunks_repacked);
            expect_equal(3, repack_stats.partial_groups_merged);
            expect_equal(4, cr.chunks().size());
            expect_equal(before_repack_size, cr.num_bytes());
            expect_equal(before_repack_blocks, cr.num_blocks());
            expect(!std::filesystem::exists(old_repack_dir));
            expect(!std::filesystem::exists(std::filesystem::path { tmp_data_dir } / "compressed" / "repack"));
            std::optional<uint64_t> prev_chunk_id {};
            for (const auto &[last_byte_offset, chunk]: cr.chunks()) {
                const auto chunk_id = cr.make_slot(chunk.first_slot).chunk_id();
                if (prev_chunk_id)
                    expect(*prev_chunk_id != chunk_id);
                prev_chunk_id = chunk_id;
                expect_equal(zstd::default_compression_level, chunk.compression_level);
                old_chunk_paths.erase(cr.full_path(chunk.rel_path()));
            }
            for (const auto &old_chunk_path: old_chunk_paths)
                expect(!std::filesystem::exists(old_chunk_path));
            chunk_registry reloaded { tmp_data_dir, chunk_registry_settings_t { .mode=chunk_registry::mode::store } };
            expect_equal(4, reloaded.chunks().size());
            expect_equal(cr.num_bytes(), reloaded.num_bytes());
            for (const auto &[last_byte_offset, chunk]: reloaded.chunks())
                expect_equal(zstd::default_compression_level, chunk.compression_level);
            const auto second_repack_stats = reloaded.repack();
            expect_equal(4, second_repack_stats.chunks_analyzed);
            expect_equal(0, second_repack_stats.chunks_repacked);
            expect_equal(0, second_repack_stats.partial_groups_merged);
            expect_equal(second_repack_stats.compressed_size_before, second_repack_stats.compressed_size_after);
            expect(!std::filesystem::exists(std::filesystem::path { tmp_data_dir } / "compressed" / "repack"));

            const auto invalid_path = reloaded.full_path(reloaded.chunks().begin()->second.rel_path());
            file::write(invalid_path, uint8_vector { 0 });
            expect(throws([&] {
                chunk_registry invalid { tmp_data_dir, chunk_registry_settings_t { .mode=chunk_registry::mode::store } };
            }));
        };
        "epoch info"_test = [&] {
            expect(throws([]{
                epoch_info::chunk_list no_chunks {};
                epoch_info { std::move(no_chunks) };
            }));
            chunk_registry cr { data_dir, chunk_registry_settings_t { .mode=chunk_registry::mode::store } };
            uint64_t total_size = 0;
            uint64_t total_compressed_size = 0;
            std::optional<cardano::block_hash> last_block_hash {};
            std::optional<uint64_t> last_slot {};
            for (const auto &[epoch, einfo]: cr.epochs()) {
                // need a strict test data set to re-enable the following check
                // if (last_block_hash)
                //    expect(*last_block_hash == einfo.prev_block_hash()) << epoch;
                last_block_hash = einfo.last_block_hash();
                if (last_slot)
                    expect(einfo.last_slot() >= *last_slot);
                last_slot = einfo.last_slot();
                expect(einfo.start_offset() == total_size);
                total_size += einfo.size();
                expect(einfo.end_offset() == total_size);
                expect(einfo.size() == einfo.end_offset() - einfo.start_offset());
                total_compressed_size += einfo.compressed_size();
            }
            expect(cr.num_bytes() == total_size);
            expect(cr.num_compressed_bytes() == total_compressed_size);
            expect(static_cast<bool>(last_block_hash));
            expect(!cr.empty());
            if (last_block_hash && cr.tip())
                expect(*last_block_hash == cr.tip()->hash);
            if (last_slot)
                expect(*last_slot == cr.max_slot());
        };
        "published transaction survives a processor finalization failure"_test = [] {
            const auto chain = sync::gen_chain({ .height=1 });
            const file::tmp_directory dir { "chunk-registry-commit-reopen" };
            file_remover remover;
            size_t commit_calls = 0;
            {
                chunk_registry cr { dir.path(), chunk_registry_settings_t { .ccfg=chain.cardano_cfg, .fr=remover } };
                const chunk_processor failing_processor {
                    .commit_tx=[&] {
                        ++commit_calls;
                        expect(!turbo::zpp::load<storage::chunk_map>(dir.path() + "/compressed/state.bin").empty());
                        throw error("injected processor finalization failure");
                    }
                };
                cr.register_processor(failing_processor);
                expect(throws([&] { (void)cr.accept_progress({}, *chain.tip, [&] { cr.add_buffer(0, chain.data); }); }));
                cr.remove_processor(failing_processor);
                expect_equal(commit_calls, 1);
                expect(cr.tx().has_value());
                expect(throws([&] { cr.maintenance(); }));
                expect(throws([&] { cr.truncate({}); }));
            }
            chunk_registry reopened { dir.path(), chunk_registry_settings_t { .ccfg=chain.cardano_cfg, .fr=remover } };
            expect_equal(reopened.tip(), chain.tip);
            expect(std::filesystem::is_empty(std::filesystem::path { dir.path() } / "transactions"));
        };

        "progress despite errors and rollback"_test = [] {
            const auto chain = sync::gen_chain({ .height=2 });
            const file::tmp_directory dir { "chunk-registry-partial-progress" };
            chunk_registry cr { dir.path(), chunk_registry_settings_t { .ccfg=chain.cardano_cfg } };
            expect(!!cr.accept_progress({}, *chain.tip, [] { throw error("injected failure"); }));
            expect(cr.empty());
            expect(!cr.tx());
            const auto first = chain.blocks.front()->blk.raw();
            expect(!!cr.accept_progress({}, *chain.tip, [&] {
                cr.add_buffer(0, uint8_vector { first });
                throw error("injected failure after progress");
            }));
            expect_equal(cr.num_bytes(), first.size());
            expect(!cr.tx());
            const auto saved_tip = cr.tip();
            expect(!!cr.accept_progress({}, *chain.tip, [&] {
                cr.add_buffer(0, uint8_vector { first });
                throw error("injected failure without progress");
            }));
            expect_equal(cr.tip(), saved_tip);
            expect(!cr.tx());
        };
    };
};
