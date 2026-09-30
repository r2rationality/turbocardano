/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <atomic>
#include <chrono>
#include <future>
#include <numeric>
#include <tuple>
#include <unordered_set>
#include <turbo/common/scope-exit.hpp>
#include <turbo/cardano.hpp>
#include <turbo/cardano/ledger/state.hpp>
#include <turbo/chunk-registry.hpp>
#include <turbo/storage/replay.hpp>

namespace turbo {
    namespace {
        using compression_level_list_t = std::vector<int32_t>;

        struct candidate_chain_rejected_t: error {
            using error::error;
        };

        // Chunk metadata supplies this relative path from its data hash, so no
        // containment or filesystem canonicalization is needed here.
        std::string trusted_chunk_path(const std::filesystem::path &db_dir, const storage::chunk_info &chunk)
        {
            auto path = db_dir / chunk.rel_path();
            path.make_preferred();
            return path.string();
        }

        constexpr bool valid_slot_successor(const uint64_t prev_slot, const uint64_t prev_era,
            const uint64_t slot, const uint64_t era)
        {
            return slot > prev_slot || (slot == prev_slot && prev_era == 0 && era == 1);
        }

        static_assert(valid_slot_successor(1, 0, 1, 1));
        static_assert(!valid_slot_successor(1, 0, 1, 2));

        void require_block_successor(const cardano::point2 &previous, const uint64_t previous_era,
            const cardano::block_hash &previous_hash, const uint64_t slot, const uint64_t era, const bool check_hash=true)
        {
            if (!valid_slot_successor(previous.slot, previous_era, slot, era)) [[unlikely]] {
                throw error(fmt::format("block slot {} is not after predecessor slot {}", slot, previous.slot));
            }
            if (check_hash && previous_hash != previous.hash) [[unlikely]] {
                throw error(fmt::format("block at slot {} has predecessor hash {} instead of {}", slot, previous_hash, previous.hash));
            }
        }

        struct orphan_chunk_t {
            storage::chunk_info source;
            uint64_t first_era = 0;
            uint64_t last_era = 0;
            uint64_t last_height = 0;
        };

        orphan_chunk_t inspect_orphan(const std::string &path, const uint64_t compressed_size,
            const std::filesystem::path &db_dir, const cardano::config &cfg, std::vector<cardano::block_hash> &hashes)
        {
            const auto raw = zstd::read(path);
            orphan_chunk_t candidate;
            auto &source = candidate.source;
            source.data_size = raw.size();
            source.compressed_size = compressed_size;
            crypto::blake2b::digest(source.data_hash, raw);
            if (std::filesystem::path { path } != std::filesystem::path { trusted_chunk_path(db_dir, source) }) [[unlikely]] {
                throw error("chunk filename does not match its content hash");
            }
            storage::block_reader_t reader { raw, 0, cfg };
            while (!reader.done()) {
                const auto block = reader.next();
                if (hashes.empty()) {
                    source.prev_block_hash = block->prev_hash();
                    source.first_slot = block->slot();
                    candidate.first_era = block->era();
                } else {
                    require_block_successor(source.last_block, candidate.last_era,
                        block->prev_hash(), block->slot(), block->era());
                }
                source.last_block = { block->slot(), block->hash() };
                candidate.last_height = block->height();
                candidate.last_era = block->era();
                hashes.push_back(source.last_block.hash);
            }
            if (hashes.empty()) [[unlikely]] {
                throw error("empty unregistered chunk");
            }
            source.num_blocks = hashes.size();
            return candidate;
        }

        std::vector<std::optional<orphan_chunk_t>> discover_orphans(const std::vector<std::string> &paths,
            const std::map<std::string, uint64_t> &sizes, const std::filesystem::path &db_dir,
            const cardano::config &cfg, scheduler &sched)
        {
            std::vector<std::optional<orphan_chunk_t>> candidates(paths.size());
            std::unordered_set<cardano::block_hash> hashes;
            std::mutex hashes_mutex;
            std::atomic_bool duplicate = false;
            storage::replay_batch_t batch { sched };
            for (size_t i = 0; i < paths.size(); ++i) {
                if (duplicate.load(std::memory_order_relaxed)) [[unlikely]] {
                    break;
                }
                uint64_t raw_bytes;
                try {
                    std::array<uint8_t, 18> header {};
                    file::read_stream stream { paths[i] };
                    const auto size = stream.try_read(header);
                    raw_bytes = zstd::decompressed_size(buffer { header.data(), size });
                    if (raw_bytes > zstd::max_zstd_buffer) [[unlikely]] {
                        throw error("oversized unregistered chunk");
                    }
                } catch (const std::exception &ex) {
                    logger::warn("cannot inspect chunk {}: {}", paths[i], ex.what());
                    continue;
                }
                const auto compressed_size = sizes.at(paths[i]);
                batch.admit(raw_bytes, compressed_size);
                if (duplicate.load(std::memory_order_relaxed)) [[unlikely]] {
                    break;
                }
                sched.submit("recover-discover", 100, [&, i, compressed_size] {
                    if (duplicate.load(std::memory_order_relaxed)) [[unlikely]] {
                        return;
                    }
                    try {
                        std::vector<cardano::block_hash> chunk_hashes;
                        auto candidate = inspect_orphan(paths[i], compressed_size, db_dir, cfg, chunk_hashes);
                        {
                            std::scoped_lock lock { hashes_mutex };
                            if (duplicate.load(std::memory_order_relaxed)) [[unlikely]] {
                                return;
                            }
                            for (const auto &hash: chunk_hashes) {
                                if (!hashes.emplace(hash).second) [[unlikely]] {
                                    duplicate.store(true, std::memory_order_relaxed);
                                    break;
                                }
                            }
                        }
                        candidates[i] = std::move(candidate);
                    } catch (const std::exception &ex) {
                        logger::warn("cannot recover chunk {}: {}", paths[i], ex.what());
                    }
                });
            }
            batch.drain();
            if (duplicate.load(std::memory_order_relaxed)) [[unlikely]] {
                logger::warn("duplicate blocks in unregistered chunks; discarding all unregistered downloads");
                return {};
            }
            return candidates;
        }

        struct orphan_chain_t {
            storage::chunk_list chunks;
            optional_progress_point target;
        };

        orphan_chain_t select_orphans(const std::vector<std::optional<orphan_chunk_t>> &candidates,
            const cardano::optional_point &start, uint64_t era, const cardano::block_hash &genesis)
        {
            std::map<cardano::block_hash, std::vector<const orphan_chunk_t *>> successors;
            size_t available = 0;
            for (const auto &candidate: candidates) {
                if (candidate) {
                    successors[candidate->source.prev_block_hash].push_back(&*candidate);
                    ++available;
                }
            }
            for (auto &[hash, chunks]: successors) {
                std::ranges::sort(chunks, [](const auto *a, const auto *b) {
                    return std::tie(a->source.first_slot, a->source.data_hash)
                        < std::tie(b->source.first_slot, b->source.data_hash);
                });
            }
            cardano::point2 previous = start ? static_cast<cardano::point2>(*start) : cardano::point2 { 0, genesis };
            uint64_t offset = start ? start->end_offset : 0;
            orphan_chain_t chain;
            for (;;) {
                const auto it = successors.find(previous.hash);
                if (it == successors.end()) {
                    break;
                }
                const orphan_chunk_t *selected = nullptr;
                for (const auto *candidate: it->second) {
                    if (offset && !valid_slot_successor(previous.slot, era, candidate->source.first_slot, candidate->first_era)) {
                        continue;
                    }
                    if (selected) {
                        logger::warn("fork in unregistered downloads after {}; keeping candidate {} and discarding alternative {}",
                            previous, selected->source.rel_path(), candidate->source.rel_path());
                    } else {
                        selected = candidate;
                    }
                }
                successors.erase(it);
                if (!selected) {
                    break;
                }
                auto source = selected->source.without_blocks();
                source.offset = offset;
                previous = source.last_block;
                offset = source.end_offset();
                era = selected->last_era;
                chain.target.emplace(source.last_block.slot, offset);
                chain.target->height = selected->last_height;
                chain.chunks.emplace_back(std::move(source));
            }
            if (available > chain.chunks.size()) {
                logger::warn("discarding {} unregistered chunks that do not connect at the selected chunk boundaries",
                    available - chain.chunks.size());
            }
            return chain;
        }

        void load_chunk_registry_state(storage::chunk_map &chunks, const std::string &path)
        {
            const auto state_data = file::read(path);
            ::zpp::bits::in in { static_cast<buffer>(state_data) };
            storage::chunk_map loaded_chunks {};
            in(loaded_chunks).or_throw();
            if (in.position() < state_data.size()) {
                compression_level_list_t compression_levels {};
                in(compression_levels).or_throw();
                if (compression_levels.size() != loaded_chunks.size()) [[unlikely]] {
                    throw error(fmt::format(
                        "chunk registry state has {} chunks but {} compression levels",
                        loaded_chunks.size(), compression_levels.size()));
                }
                auto level_it = compression_levels.begin();
                for (auto &[last_byte_offset, chunk]: loaded_chunks) {
                    chunk.compression_level = *level_it++;
                }
            }
            chunks = std::move(loaded_chunks);
        }

        std::filesystem::path revalidation_source_path(const std::filesystem::path &data_dir)
        {
            return data_dir / "compressed/revalidate-source.bin";
        }

        void restore_revalidation_source(const std::filesystem::path &data_dir)
        {
            const auto source = revalidation_source_path(data_dir);
            if (!std::filesystem::exists(source))
                return;
            const auto state = data_dir / "compressed/state.bin";
            const auto temporary = state.string() + ".restore";
            std::filesystem::copy_file(source, temporary, std::filesystem::copy_options::overwrite_existing);
            std::filesystem::rename(temporary, state);
            logger::info("restored stored-chain metadata for interrupted revalidation");
        }

        void save_chunk_registry_state(const std::string &path, const storage::chunk_map &chunks)
        {
            compression_level_list_t compression_levels {};
            compression_levels.reserve(chunks.size());
            for (const auto &[last_byte_offset, chunk]: chunks) {
                compression_levels.emplace_back(chunk.compression_level);
            }
            uint8_vector state_data {};
            ::zpp::bits::out out { state_data };
            out(chunks, compression_levels).or_throw();
            file::write(path, state_data);
        }
    }

    namespace {
        void link_checkpoint_file(const std::filesystem::path &from, const std::filesystem::path &to)
        {
            std::filesystem::create_directories(to.parent_path());
            if (std::filesystem::exists(to) && std::filesystem::equivalent(from, to))
                return;
            const auto tmp = to.string() + ".restore";
            std::error_code ec;
            std::filesystem::remove(tmp, ec);
            std::filesystem::create_hard_link(from, tmp);
            std::filesystem::rename(tmp, to);
        }

        std::filesystem::path snapshot_relative_path(const uint64_t offset)
        {
            return std::filesystem::path { "validate" } / fmt::format("ledger-{:013}.bin", offset);
        }

        struct checkpoint_manifest_t {
            static constexpr uint64_t current_version = 2;

            std::filesystem::path directory;
            uint64_t version;
            uint64_t offset;
            uint64_t height;
            std::string hash;
            std::vector<std::filesystem::path> files;
            validator::snapshot_set snapshots;

            explicit checkpoint_manifest_t(const std::filesystem::path &dir): directory { dir }
            {
                const auto manifest = json::load((dir / "manifest.json").string());
                version = json::value_to<uint64_t>(manifest.at("version"));
                if (version != 1 && version != current_version)
                    throw error("unsupported checkpoint version");
                offset = json::value_to<uint64_t>(manifest.at("offset"));
                height = json::value_to<uint64_t>(manifest.at("height"));
                hash = json::value_to<std::string>(manifest.at("hash"));
                for (const auto &file: manifest.at("files").as_array()) {
                    files.emplace_back(json::value_to<std::string>(file));
                    if (!std::filesystem::exists(dir / files.back()))
                        throw error("checkpoint file is missing");
                }
                for (const auto &required: { "index/state.json", "validate/state.json" })
                    if (std::ranges::find(files, std::filesystem::path { required }) == files.end())
                        throw error("checkpoint metadata is missing from the manifest");
                uint64_t indexed_offset = 0;
                const auto index_state = json::load((dir / "index/state.json").string());
                for (const auto &value: index_state.as_array()) {
                    const auto slice = indexer::merger::slice::from_json(value.as_object());
                    if (slice.offset != indexed_offset || slice.offset > offset || slice.size > offset - slice.offset)
                        throw error("checkpoint index slices do not form a continuous prefix");
                    indexed_offset = slice.end_offset();
                }
                if (indexed_offset != offset)
                    throw error("checkpoint index boundary does not match its manifest");
                const auto state = json::load((dir / "validate/state.json").string());
                if (version == current_version && state.as_array().size() != 1)
                    throw error("checkpoint must contain exactly one ledger snapshot");
                for (const auto &value: state.as_array()) {
                    auto snap = validator::snapshot::from_json(value);
                    if (snap.end_offset > offset || (version == current_version && snap.end_offset != offset))
                        throw error("checkpoint ledger boundary does not match its manifest");
                    if (std::ranges::find(files, snapshot_relative_path(snap.end_offset)) == files.end())
                        throw error("ledger snapshot is missing from the manifest");
                    snapshots.emplace(std::move(snap));
                }
            }

            bool matches(const cardano::point &point) const
            {
                return offset == point.end_offset && height == point.height && hash == fmt::format("{}", point.hash);
            }

            bool belongs_to(const storage::chunk_map &chunks) const
            {
                if (!offset)
                    return true;
                const auto chunk = chunks.lower_bound(offset - 1);
                if (chunk == chunks.end())
                    return false;
                const auto &blocks = chunk->second.blocks;
                const auto block = std::ranges::lower_bound(blocks, offset, {}, &storage::block_info::end_offset);
                return block != blocks.end() && matches(block->point());
            }
        };

        std::vector<checkpoint_manifest_t> read_checkpoints(const std::filesystem::path &root)
        {
            std::vector<checkpoint_manifest_t> checkpoints;
            if (std::filesystem::exists(root)) {
                for (const auto &entry: std::filesystem::directory_iterator(root)) {
                    if (!std::filesystem::exists(entry.path() / "manifest.json"))
                        continue;
                    try {
                        checkpoints.emplace_back(entry.path());
                    } catch (const std::exception &ex) {
                        logger::warn("ignoring incomplete checkpoint {}: {}", entry.path().string(), ex.what());
                    }
                }
            }
            std::ranges::sort(checkpoints, [](const auto &a, const auto &b) {
                return std::tie(a.offset, a.directory) < std::tie(b.offset, b.directory);
            });
            return checkpoints;
        }

        void restore_checkpoint(const std::filesystem::path &data_dir, const storage::chunk_map &chunks)
        {
            const auto root = data_dir / "checkpoints";
            if (!std::filesystem::exists(root))
                return; // legacy database: recover its independent processor positions
            auto candidates = read_checkpoints(root);
            std::erase_if(candidates, [&](const auto &checkpoint) { return !checkpoint.belongs_to(chunks); });
            if (!candidates.empty()) {
                const auto &newest = candidates.back();
                for (const auto &file: newest.files)
                    link_checkpoint_file(newest.directory / file, data_dir / file);
                validator::snapshot_set snapshots;
                for (const auto &candidate: candidates | std::views::reverse) {
                    if (snapshots.size() == 2)
                        break;
                    for (const auto &snap: candidate.snapshots | std::views::reverse) {
                        if (snapshots.size() == 2)
                            break;
                        if (snapshots.emplace(snap).second) {
                            const auto file = snapshot_relative_path(snap.end_offset);
                            link_checkpoint_file(candidate.directory / file, data_dir / file);
                        }
                    }
                }
                json::array state;
                for (const auto &snap: snapshots)
                    state.emplace_back(snap.to_json());
                json::save_pretty((data_dir / "validate/state.json").string(), state);
                logger::info("restored coordinated checkpoint at offset {}", newest.offset);
            } else {
                // No checkpoint belongs to the stored branch. Derived state is
                // disposable; retain blocks and rebuild from genesis.
                std::filesystem::create_directories(data_dir / "index");
                std::filesystem::create_directories(data_dir / "validate");
                json::save_pretty((data_dir / "index/state.json").string(), json::array {});
                json::save_pretty((data_dir / "validate/state.json").string(), json::array {});
            }
        }
    }

    chunk_registry::chunk_registry(const std::string &data_dir, chunk_registry_settings_t settings)
        : _continuous { settings.continuous }, _mode { settings.mode }, _data_dir { std::filesystem::weakly_canonical(data_dir) },
            _db_dir { settings.mode == mode::validate ? init_db_dir((_data_dir / "compressed").string())
                : std::filesystem::weakly_canonical(_data_dir / "compressed") },
            _cardano_cfg { std::move(settings.ccfg) }, _sched { settings.sched }, _file_remover { settings.fr },
            _state_path { (_db_dir / "state.bin").string() }
    {
        timer t { "chunk-registry construct" };
        if (settings.recover_orphans && _mode != mode::validate)
            throw error("orphan recovery requires a validating registry: '{}'", _data_dir);
        if (_mode == mode::validate)
            _writer_lock = std::make_unique<storage::registry_writer_lock>(_data_dir);
        storage::commit_journal::recover(_data_dir, _mode == mode::validate);
        if (settings.mode != mode::validate && std::filesystem::exists(revalidation_source_path(_data_dir))) [[unlikely]] {
            throw error("interrupted revalidation requires opening the registry in validation mode first");
        }
        if (settings.mode == mode::validate) {
            restore_revalidation_source(_data_dir);
        }
        chunk_map chunks;
        const bool state_exists = std::filesystem::exists(_state_path);
        _state_needs_save = !state_exists;
        if (!state_exists && _mode != mode::validate) [[unlikely]] {
            throw error("missing registry state: open the registry in validation mode first");
        }
        // Independent I/O: merge the inventory with metadata only after both finish.
        auto inventory = std::async(std::launch::async, [this] { return _scan_storage(); });
        if (state_exists) {
            load_chunk_registry_state(chunks, _state_path);
        }
        auto scan = inventory.get();
        std::optional<indexer::slice_list> index_slices;
        file_remover::remove_point_map marked;
        if (settings.mode == mode::validate) {
            restore_checkpoint(_data_dir, chunks);
            std::filesystem::create_directories(_db_dir / "chunk");
        } else {
            _require_clean_storage(scan, chunks);
            marked = _file_remover.removable();
        }
        _repair_or_require_clean_storage(scan, chunks, marked);
        if (_mode != mode::validate) {
            index_slices = _inspect_derived_state(chunks, marked);
        }
        std::unique_ptr<indexer::incremental> loaded_indexer {};
        std::unique_ptr<validator::incremental> loaded_validator {};
        size_t init_task_idx = 0;
        switch (settings.mode) {
            case mode::validate: {
                _sched.submit("chunk-registry-init:indexer", static_cast<int64_t>(init_task_idx++), [&] {
                    loaded_indexer = std::make_unique<indexer::incremental>(
                        *this, validator::default_indexers(_data_dir.string(), _sched), chunks);
                });
                _sched.submit("chunk-registry-init:validator", static_cast<int64_t>(init_task_idx++), [&] {
                    loaded_validator = std::make_unique<validator::incremental>(*this, chunks, settings.validate_vrf);
                });
                break;
            }
            case mode::index: {
                _sched.submit("chunk-registry-init:indexer", static_cast<int64_t>(init_task_idx++), [&] {
                    loaded_indexer = std::make_unique<indexer::incremental>(
                        *this, indexer::default_list(_data_dir.string(), _sched), chunks, std::move(index_slices));
                });
                break;
            }
            case mode::store:
                // do nothing
                break;
            [[unlikely]] default:
                throw error(fmt::format("unsupported mode: {}", static_cast<int>(settings.mode)));
        }
        if (init_task_idx > 0) {
            _sched.process(true);
        }
        _indexer = std::move(loaded_indexer);
        _validator = std::move(loaded_validator);

        std::vector<std::string> recovery_inputs;
        std::vector<std::shared_ptr<void>> revalidation_pins;
        if (std::filesystem::exists(revalidation_source_path(_data_dir))) {
            for (const auto &[offset, chunk]: chunks) {
                recovery_inputs.emplace_back(trusted_chunk_path(_db_dir, chunk));
                revalidation_pins.emplace_back(_file_remover.pin(recovery_inputs.back()));
            }
        }
        scope_exit preserve_revalidation_source { [&] {
            if (std::filesystem::exists(revalidation_source_path(_data_dir))) {
                for (const auto &path: recovery_inputs) {
                    _file_remover.unmark(path);
                }
            }
        }};

        for (auto &&[last_byte_offset, chunk]: chunks) {
            _add(std::move(chunk), false);
        }
        logger::info("chunk_registry has data up to offset {}", num_bytes());
        _state_needs_save |= _chunks.size() != chunks.size();
        if (_mode != mode::validate && (_chunks.size() != chunks.size() || !_unmerged_chunks.empty())) [[unlikely]] {
            throw error("inconsistent registry metadata: open the registry in validation mode first");
        }
        if (_mode == mode::validate) {
            _maintenance(std::move(scan), settings.recover_orphans || !state_exists);
            revalidation_pins.clear();
            _file_remover.remove();
        }
    }

    chunk_registry::~chunk_registry() =default;

    void chunk_registry::register_processor(const chunk_processor &p)
    {
        mutex::scoped_lock lk { _processors_mutex };
        _processors.emplace(&p);
    }

    void chunk_registry::remove_processor(const chunk_processor &p)
    {
        mutex::scoped_lock lk { _processors_mutex };
        _processors.erase(&p);
    }

    void chunk_registry::report_progress(const std::string_view name, const progress_point &tip) const
    {
        if (_transaction) [[likely]] {
            uint64_t rel_pos = 0;
            uint64_t rel_target = 0;
            // prefer to compute the progress using offsets, fallback to slots if not available
            if (_transaction->target->end_offset) {
                rel_pos = tip.end_offset;
                rel_target = _transaction->target->end_offset;
            } else {
                rel_pos = tip.slot;
                rel_target = _transaction->target->slot;
            }
            uint64_t prev_pos = 0;
                {
                mutex::scoped_lock lk { _tx_progress_mutex };
                if (const auto [it, created] = _tx_progress_max.try_emplace(std::string { name }, rel_pos); !created) {
                    prev_pos = it->second;
                    if (it->second < rel_pos)
                        it->second = rel_pos;
                }
                }
            if (prev_pos < rel_pos) {
                for (const auto *p: _processors) {
                    if (p->on_progress)
                        p->on_progress(name, rel_pos, rel_target);
                }
            }
        } else {
            throw error("report_progress can be called only inside of a transaction");
        }
    }

    validator::validation_mode chunk_registry::validation(const validator::validation_mode mode)
    {
        if (!_validator) {
            if (mode != validator::validation_mode::none)
                throw error("witness validation requires a validating registry");
            return mode;
        }
        return _validator->validation(mode);
    }

    void chunk_registry::_recover_registered()
    {
        if (_chunks.empty()) {
            if (max_end_offset()) [[unlikely]] {
                truncate({});
            }
            return;
        }
        const auto stored_tip = _chunks.rbegin()->second.blocks.back().point();
        const auto start = tip();
        if (start == cardano::optional_point { stored_tip }) {
            if (max_end_offset() != num_bytes()) [[unlikely]] {
                truncate(start);
            }
            return;
        }
        const auto start_offset = start ? start->end_offset : 0;
        chunk_list tail;
        for (const auto &[offset, chunk]: _chunks) {
            if (chunk.end_offset() > start_offset) {
                tail.emplace_back(chunk.without_blocks());
            }
        }
        logger::info("replaying stored blocks from {} to {}", start, stored_tip);
        const auto previous_mode = validation(validator::validation_mode::full);
        scope_exit restore_settings { [&] {
            if (!tx()) [[likely]] {
                validation(previous_mode);
            }
        }};
        _replay(tail, start, stored_tip, validator::snapshot_policy::catchup_interval);
    }

    void chunk_registry::revalidate(cardano::optional_point target,
        const std::chrono::steady_clock::duration checkpoint_interval)
    {
        if (_transaction || _journal || !_validator || !_indexer) [[unlikely]] {
            throw error("revalidation requires an idle validating registry");
        }
        if (target) {
            const auto it = _chunks.find(target->end_offset - 1);
            if (it == _chunks.end() || it->second.blocks.back().point() != *target) [[unlikely]] {
                throw error("revalidation target must be a registered chunk boundary");
            }
            target = it->second.blocks.back().point();
        }
        chunk_list chunks;
        const auto source = revalidation_source_path(_data_dir);
        if (!std::filesystem::exists(source)) {
            const auto temporary = source.string() + ".tmp";
            std::filesystem::copy_file(_state_path, temporary, std::filesystem::copy_options::overwrite_existing);
            std::filesystem::rename(temporary, source);
        }
        std::vector<std::shared_ptr<void>> input_pins;
        std::vector<std::string> input_paths;
        for (const auto &[offset, chunk]: _chunks) {
            input_paths.emplace_back(full_path(chunk.rel_path()));
            input_pins.emplace_back(_file_remover.pin(input_paths.back()));
            if (target && chunk.end_offset() <= target->end_offset) {
                chunks.emplace_back(chunk.without_blocks());
            }
        }
        scope_exit preserve_input { [&] {
            if (std::filesystem::exists(source)) {
                for (const auto &path: input_paths) {
                    _file_remover.unmark(path);
                }
            }
        }};
        _replay(chunks, {}, target, checkpoint_interval, replay_mode::revalidation);
    }

    chunk_registry::maintenance_scan_t chunk_registry::_scan_storage() const
    {
        maintenance_scan_t scan;
        for (const auto *name: { "compressed/state.bin", "compressed/state-pre.bin", "compressed/revalidate-source.bin",
                "index/state.json", "index/state-pre.json" }) {
            for (const auto *suffix: { ".tmp", ".restore" }) {
                const auto path = (_data_dir / name).string() + suffix;
                if (std::filesystem::exists(path)) {
                    scan.temporary.emplace_back(path);
                }
            }
        }
        const auto chunk_dir = _db_dir / "chunk";
        if (!std::filesystem::exists(chunk_dir)) {
            return scan;
        }
        for (const auto &entry: std::filesystem::directory_iterator { chunk_dir }) {
            if (!entry.is_regular_file()) [[unlikely]] {
                throw error(fmt::format("unexpected entry in chunk directory: {}", entry.path().string()));
            }
            auto path = entry.path();
            path.make_preferred();
            const auto extension = path.extension();
            if (extension != ".zstd" && extension != ".tmp") [[unlikely]] {
                throw error(fmt::format("unexpected file in chunk directory: {}; expected .zstd or .tmp", path.string()));
            }
            if (extension == ".tmp") {
                scan.temporary.emplace_back(path.string());
                continue;
            }
            scan.chunks.emplace(path.string(), entry.file_size());
        }
        return scan;
    }

    void chunk_registry::_require_clean_storage(const maintenance_scan_t &scan, const chunk_map &chunks) const
    {
        if (!std::filesystem::is_regular_file(_state_path) || std::filesystem::exists(_db_dir / "state-pre.bin")
                || std::filesystem::exists(revalidation_source_path(_data_dir))) [[unlikely]] {
            throw error("unfinished registry state: open the registry in validation mode first");
        }
        // Reconciliation below verifies each registered path; matching counts
        // then exclude unregistered files without building another path set.
        if (scan.chunks.size() != chunks.size() || !scan.temporary.empty()) [[unlikely]] {
            throw error("registry recovery or cleanup required: open the registry in validation mode first");
        }
    }

    void chunk_registry::_repair_or_require_clean_storage(const maintenance_scan_t &scan, chunk_map &chunks,
        const file_remover::remove_point_map &marked)
    {
        std::vector<std::pair<chunk_info *, uint64_t>> repairs;
        for (auto chunk_it = chunks.begin(); chunk_it != chunks.end(); ++chunk_it) {
            auto &chunk = chunk_it->second;
            const auto path = trusted_chunk_path(_db_dir, chunk);
            const auto file_it = scan.chunks.find(path);
            if (_mode != mode::validate && (file_it == scan.chunks.end()
                    || file_it->second != chunk.compressed_size || marked.contains(path))) [[unlikely]] {
                throw error(fmt::format("chunk {} requires repair: open the registry in validation mode first", path));
            }
            if (file_it == scan.chunks.end()) [[unlikely]] {
                logger::info("load_state: missing file {} - ignoring it and the following chunks!", chunk.rel_path());
                chunks.erase(chunk_it, chunks.end());
                _state_needs_save = true;
                break;
            }
            const auto file_size = file_it->second;
            if (file_size == chunk.compressed_size) {
                continue;
            }
            logger::warn(
                "load_state: validating stale compression metadata for {}: recorded size: {} actual size: {}",
                chunk.rel_path(), chunk.compressed_size, file_size);
            const auto expected_size = chunk.data_size;
            const auto expected_hash = chunk.data_hash;
            _sched.submit("chunk-registry-check", -static_cast<int64_t>(repairs.size()),
                [path, expected_size, expected_hash] {
                    const auto uncompressed = zstd::read(path);
                    if (uncompressed.size() != expected_size) [[unlikely]] {
                        throw error(fmt::format(
                            "chunk {} decompressed to {} bytes instead of the recorded {}",
                            path, uncompressed.size(), expected_size));
                    }
                    cardano::block_hash data_hash {};
                    crypto::blake2b::digest(data_hash, uncompressed);
                    if (data_hash != expected_hash) [[unlikely]] {
                        throw error(fmt::format(
                            "chunk {} has uncompressed data hash {} instead of the recorded {}",
                            path, data_hash, expected_hash));
                    }
                });
            repairs.emplace_back(&chunk, file_size);
        }
        if (!repairs.empty()) {
            _sched.process(true);
            // Workers only validate bytes. Publish metadata changes together
            // after every check has succeeded.
            for (const auto &[chunk, size]: repairs) {
                chunk->compressed_size = size;
                chunk->compression_level = 0;
            }
            _state_needs_save = true;
            logger::warn("load_state: repaired stale compression metadata for {} chunks; compression levels are now unknown",
                repairs.size());
        }
    }

    indexer::slice_list chunk_registry::_inspect_derived_state(const chunk_map &chunks, const file_remover::remove_point_map &marked) const
    {
        const auto stored_end = chunks.empty() ? 0 : chunks.rbegin()->second.end_offset();
        auto slices = indexer::inspect_state(_data_dir, chunks, marked, _mode == mode::index);
        const auto indexed_end = slices.empty() ? 0 : slices.back().end_offset();
        if ((_mode == mode::index || std::filesystem::exists(_data_dir / "index/state.json")) && indexed_end != stored_end) [[unlikely]] {
            throw error("indexes do not cover the stored chain: open the registry in validation mode first");
        }
        const auto dir = std::filesystem::weakly_canonical(_data_dir / "validate");
        const auto path = dir / "state.json";
        if (!std::filesystem::exists(dir)) {
            return slices;
        }
        if (!std::filesystem::is_regular_file(path)) [[unlikely]] {
            throw error("missing ledger state metadata: open the registry in validation mode first");
        }
        std::set<std::filesystem::path> known { path };
        const auto state = json::load(path.string());
        if (state.as_array().size() > 2) [[unlikely]] {
            throw error("ledger snapshot cleanup required: open the registry in validation mode first");
        }
        for (const auto &value: state.as_array()) {
            const auto snap = validator::snapshot::from_json(value);
            const auto file = dir / snapshot_relative_path(snap.end_offset).filename();
            if (!snap.matches(chunks) || !std::filesystem::is_regular_file(file)
                    || marked.contains(file.string()) || !known.emplace(file).second) [[unlikely]] {
                throw error("inconsistent ledger snapshot: open the registry in validation mode first");
            }
        }
        for (const auto &entry: std::filesystem::directory_iterator(dir)) {
            if (entry.is_regular_file() && !known.contains(entry.path())) [[unlikely]] {
                throw error("ledger cleanup required: open the registry in validation mode first");
            }
        }
        return slices;
    }

    void chunk_registry::_recover_orphans(const std::vector<std::string> &paths, const maintenance_scan_t &scan)
    {
        if (paths.empty()) {
            return;
        }
        logger::info("examining {} unregistered chunk files for recovery", paths.size());
        const auto start = tip();
        const auto start_era = start ? find_block_by_offset(start->end_offset - 1).era : 0;
        const auto chain = [&] {
            const auto candidates = discover_orphans(paths, scan.chunks, _db_dir, _cardano_cfg, _sched);
            return select_orphans(candidates, start, start_era, _cardano_cfg.byron_genesis_hash);
        }();
        if (chain.chunks.empty()) {
            return;
        }
        logger::info("recovering {} downloaded chunks from offset {} to {}", chain.chunks.size(),
            start ? start->end_offset : 0, chain.chunks.back().end_offset());
        const auto previous_mode = validation(validator::validation_mode::full);
        scope_exit restore_mode { [&] {
            if (!tx()) [[likely]] {
                validation(previous_mode);
            }
        }};
        _replay(chain.chunks, start, chain.target, validator::snapshot_policy::catchup_interval, replay_mode::orphans);
    }

    void chunk_registry::_replay(const chunk_list &chunks, const cardano::optional_point &start,
        optional_progress_point target, const std::chrono::steady_clock::duration checkpoint_interval,
        const replay_mode mode)
    {
        // Transactions can truncate or supersede these files before later
        // batches consume them. Keep the originals available across checkpoints.
        std::vector<std::string> paths;
        std::vector<std::shared_ptr<void>> pins;
        paths.reserve(chunks.size());
        pins.reserve(chunks.size());
        for (const auto &chunk: chunks) {
            paths.emplace_back(trusted_chunk_path(_db_dir, chunk));
            pins.emplace_back(_file_remover.pin(paths.back()));
        }
        bool complete = false;
        scope_exit preserve_input { [&] {
            if (!complete) [[unlikely]] {
                for (const auto &path: paths) {
                    _file_remover.unmark(path);
                }
            }
        }};
        const auto start_offset = start ? start->end_offset : 0;
        auto last_checkpoint = std::chrono::steady_clock::now();
        if (target) {
            target->final_checkpoint = true;
        }
        size_t next = 0;
        bool external_check_failed = false;
        const auto previous_check = std::exchange(_before_commit, {});
        scope_exit restore_check { [&] { _before_commit = previous_check; } };
        _before_commit = [&] {
            const auto actual = tip();
            const bool reached = next
                ? actual && static_cast<cardano::point2>(*actual) == chunks.at(next - 1).last_block
                : actual == start;
            if (!reached) [[unlikely]] {
                throw error("revalidation did not reach the submitted prefix");
            }
            if (previous_check) {
                try {
                    previous_check();
                } catch (...) {
                    external_check_failed = true;
                    throw;
                }
            }
        };
        do {
            const auto committed_tip = next ? tip() : start;
            const auto failure = _accept_progress(committed_tip, target, false, [&] {
                storage::replay_batch_t batch { _sched };
                while (next < chunks.size()) {
                    const auto epoch = make_slot(chunks.at(next).first_slot).epoch();
                    for (; next < chunks.size() && make_slot(chunks.at(next).first_slot).epoch() == epoch; ++next) {
                        const auto &chunk = chunks.at(next);
                        const auto &path = paths.at(next);
                        const auto replay_offset = next ? chunks.at(next - 1).end_offset() : start_offset;
                        // A partial chunk is recompressed after trimming its prefix.
                        // Budget for the larger of the input and output allocations.
                        const auto compressed_bytes = chunk.offset < replay_offset
                            ? std::max<uint64_t>(chunk.compressed_size, ZSTD_compressBound(chunk.data_size))
                            : chunk.compressed_size;
                        batch.admit(chunk.data_size, compressed_bytes);
                        _sched.submit("parse", 100, [this, &chunk, path, replay_offset] {
                            if (chunk.offset < replay_offset) {
                                const auto bytes = zstd::read(path);
                                const auto skip = replay_offset - chunk.offset;
                                if (skip >= bytes.size()) [[unlikely]] {
                                    throw error("invalid replay suffix");
                                }
                                const auto suffix = static_cast<buffer>(bytes).subbuf(skip);
                                const auto compressed = zstd::compress(suffix, default_compression_level);
                                add_buffer_trusted(replay_offset, suffix, compressed, default_compression_level);
                            } else {
                                add_file(chunk.offset, path, chunk.compression_level);
                            }
                        });
                    }
                    batch.drain();
                    _my_prepare_tx();
                    _validator->flush();
                    if (std::chrono::steady_clock::now() - last_checkpoint >= checkpoint_interval && next < chunks.size()) {
                        _validator->request_checkpoint();
                    }
                    if (checkpoint_requested()) {
                        break;
                    }
                }
                // Retire the recovery source in the same commit as the final
                // replayed state, including a deliberate truncation to genesis.
                if (mode == replay_mode::revalidation && next == chunks.size())
                    _journal->remove("compressed/revalidate-source.bin");
            });
            if (failure) [[unlikely]] {
                // Only orphan replay is best-effort. Registered-chain recovery,
                // explicit commit hooks, and failed rollback must still propagate.
                if (mode != replay_mode::orphans || external_check_failed || tx()
                        || tip() != committed_tip || num_bytes() != (committed_tip ? committed_tip->end_offset : 0)
                        || valid_end_offset() != max_end_offset()) [[unlikely]] {
                    std::rethrow_exception(failure);
                }
                logger::warn("discarding unregistered candidate after replay failure; restored tip: {}", committed_tip);
                return;
            }
            checkpoint(next == chunks.size());
            last_checkpoint = std::chrono::steady_clock::now();
        } while (next < chunks.size());
        complete = true;
    }

    struct chunk_registry::repack_plan_t {
        struct item_t {
            chunk_info chunk {};
            std::vector<chunk_map::const_iterator> sources {};
            std::string path {};
            std::shared_ptr<void> pin {};
            bool recent = false;
        };

        std::filesystem::path directory;
        std::map<uint64_t, item_t> items;
        repack_stats_t stats;

        std::unique_ptr<storage::commit_journal> journal;
    };

    std::vector<cardano::point> chunk_registry::checkpoint_points() const
    {
        std::vector<cardano::point> points;
        if (_validator)
            for (const auto &snap: _validator->snapshots() | std::views::reverse)
                points.emplace_back(find_block_by_offset(snap.end_offset - 1).point());
        return points;
    }

    chunk_registry::repack_stats_t chunk_registry::checkpoint(const bool force)
    {
        if (_transaction || _journal)
            throw error("checkpoint requires an idle registry");
        if (!_validator || !_indexer)
            return {};
        if (valid_end_offset() != max_end_offset())
            throw error("checkpoint requires matching chain, index, and ledger boundaries");
        const auto p = tip();
        if (!p)
            return {};
        const auto root = _data_dir / "checkpoints";
        _validator->checkpoint(force);
        const auto points = checkpoint_points();
        if (points.empty() || (!force && points.front().end_offset == _coordinated_checkpoint_offset))
            return {};
        std::map<uint64_t, std::filesystem::path> retained;
        for (const auto &checkpoint: read_checkpoints(root)) {
            if (checkpoint.version != checkpoint_manifest_t::current_version
                    || std::ranges::none_of(points, [&](const auto &point) { return checkpoint.matches(point); }))
                continue;
            const auto &snapshot = *checkpoint.snapshots.begin();
            const auto *current = _validator->snapshots().at_offset(checkpoint.offset);
            const auto rel = snapshot_relative_path(checkpoint.offset);
            if (current && snapshot == *current && std::filesystem::equivalent(_data_dir / rel, checkpoint.directory / rel))
                retained.try_emplace(checkpoint.offset, checkpoint.directory);
        }
        std::future<std::unique_ptr<repack_plan_t>> repacking;
        if (retained.size() != points.size()) {
            logger::run_log_errors([&] {
                repacking = std::async(std::launch::async, [this] {
                    std::unique_ptr<repack_plan_t> plan;
                    logger::run_log_errors([&] { plan = _prepare_repack(repack_mode_t::merge_closed); });
                    return plan;
                });
            });
            const auto live_slices = _indexer->slices();
            for (const auto &point: points | std::views::reverse)
                if (!retained.contains(point.end_offset))
                    retained.emplace(point.end_offset, _save_checkpoint(point, live_slices));
            logger::info("saved coordinated checkpoint at {}", points.front());
        }
        for (const auto &entry: std::filesystem::directory_iterator(root))
            if (entry.is_directory() && std::ranges::none_of(retained, [&](const auto &r) { return r.second == entry.path(); }))
                std::filesystem::remove_all(entry.path());
        _file_remover.remove();
        repack_stats_t repacked;
        if (repacking.valid()) {
            auto plan = repacking.get();
            if (plan) {
                const auto failure = logger::run_log_errors([&] { repacked = _commit_repack(*plan); });
                // Optional repacking may fail before publication. A pending
                // commit instead requires recovery before the registry is used.
                if (failure && _journal && _journal->decided())
                    std::rethrow_exception(failure);
            }
        }
        _coordinated_checkpoint_offset = points.front().end_offset;
        _checkpoint_requested.store(false, std::memory_order_release);
        return repacked;
    }

    std::filesystem::path chunk_registry::_save_checkpoint(const cardano::point &point, const indexer::slice_list &live_slices)
    {
        const auto *snapshot = _validator->snapshots().at_offset(point.end_offset);
        if (!snapshot)
            throw error("checkpoint ledger snapshot is missing");
        storage::commit_journal staging { _data_dir };
        const auto directory = staging.stage("checkpoint");
        const auto published = _data_dir / "checkpoints" / fmt::format("{}-{}", point.end_offset, staging.id());
        scope_exit reset_index_paths { [&] {
            logger::run_log_errors([&] { _sched.process(true); });
            for (const auto &[name, index]: _indexer->indexers())
                index->work_dir({});
        } };
        for (const auto &[name, index]: _indexer->indexers())
            index->work_dir((directory / "index").string());
        std::filesystem::create_directories(directory / "index");
        std::filesystem::create_directories(directory / "validate");
        indexer::slice_list slices;
        for (const auto &slice: live_slices) {
            if (slice.offset >= point.end_offset)
                break;
            if (slice.end_offset() <= point.end_offset) {
                slices.emplace_back(slice);
                continue;
            }
            indexer::merger::slice tail { slice.offset, point.end_offset - slice.offset, point.slot, "checkpoint" };
            for (const auto &[name, index]: _indexer->indexers()) {
                if (!index->mergeable())
                    continue;
                index->schedule_truncate(slice.slice_id, tail.slice_id, point.end_offset);
            }
            _sched.process(true);
            slices.emplace_back(std::move(tail));
            break;
        }
        json::array files;
        const auto link_file = [&](const std::filesystem::path &relative_path) {
            if (!std::filesystem::exists(directory / relative_path))
                link_checkpoint_file(_data_dir / relative_path, directory / relative_path);
            files.emplace_back(relative_path.generic_string());
        };
        json::array index_state;
        for (const auto &slice: slices)
            index_state.emplace_back(slice.to_json());
        json::save_pretty((directory / "index/state.json").string(), index_state);
        files.emplace_back("index/state.json");
        for (const auto &[name, index]: _indexer->indexers()) {
            if (!index->mergeable())
                continue;
            for (const auto &slice: slices) {
                const auto filename = std::filesystem::path { index->reader_path(slice.slice_id) }.filename();
                link_file(std::filesystem::path { "index" } / name / filename);
            }
        }
        json::save_pretty((directory / "validate/state.json").string(), json::array { snapshot->to_json() });
        files.emplace_back("validate/state.json");
        link_file(snapshot_relative_path(snapshot->end_offset));
        json::save_pretty((directory / "manifest.json").string(), json::object {
            { "version", checkpoint_manifest_t::current_version }, { "offset", point.end_offset }, { "height", point.height },
            { "hash", fmt::format("{}", point.hash) }, { "files", std::move(files) }
        });
        // A checkpoint is independent of live state and publishes with one rename.
        std::filesystem::create_directories(published.parent_path());
        std::filesystem::rename(directory, published);
        return published;
    }

    void chunk_registry::maintenance(const bool recover_orphans)
    {
        if (_transaction || _journal) [[unlikely]] {
            throw error("registry maintenance requires an idle registry");
        }
        if (recover_orphans && _mode != mode::validate)
            throw error("orphan recovery requires a validating registry: '{}'", _data_dir);
        auto scan = _scan_storage();
        if (_mode != mode::validate) {
            _require_clean_storage(scan, _chunks);
            const auto marked = _file_remover.removable();
            _repair_or_require_clean_storage(scan, _chunks, marked);
            _inspect_derived_state(_chunks, marked);
            return;
        }
        _maintenance(std::move(scan), recover_orphans);
        _file_remover.remove();
    }

    void chunk_registry::_maintenance(maintenance_scan_t scan, const bool recover_orphans)
    {
        _file_remover.unmark(_state_path);
        for (const auto &path: scan.temporary) {
            _file_remover.mark(path, std::chrono::seconds { 0 });
        }
        for (const auto &[offset, chunk]: _chunks) {
            const auto path = trusted_chunk_path(_db_dir, chunk);
            _file_remover.unmark(path);
            scan.chunks.erase(path);
        }
        std::vector<std::string> candidates;
        std::vector<std::shared_ptr<void>> pins;
        if (recover_orphans) {
            const auto obsolete = _file_remover.removable();
            for (const auto &[path, size]: scan.chunks) {
                if (obsolete.contains(path))
                    continue;
                candidates.emplace_back(path);
                pins.emplace_back(_file_remover.pin(path));
            }
        }
        _recover_registered();
        _recover_orphans(candidates, scan);
        if (valid_end_offset() != max_end_offset()) [[unlikely]] {
            throw error("registry maintenance did not restore a consistent boundary");
        }
        // Publish the reconciled registry before deleting recovery inputs or obsolete files.
        // Startup repairs use the same publication protocol as normal transactions.
        std::vector<std::string> obsolete_state;
        for (const auto *relative: { "compressed/revalidate-source.bin", "compressed/state-pre.bin", "index/state-pre.json" })
            if (std::filesystem::exists(_data_dir / relative))
                obsolete_state.emplace_back(relative);
        if (_state_needs_save || !obsolete_state.empty()) {
            auto journal = std::make_unique<storage::commit_journal>(_data_dir);
            for (const auto &relative: obsolete_state)
                journal->remove(relative);
            _commit_state(std::move(journal));
        }
        pins.clear();
        // Reconcile the original inventory with the final registry. Files made
        // obsolete during replay/repacking are already tracked by the remover.
        for (const auto &[offset, chunk]: _chunks) {
            scan.chunks.erase(trusted_chunk_path(_db_dir, chunk));
        }
        for (const auto &[path, size]: scan.chunks) {
            _file_remover.mark(path, std::chrono::seconds { 0 });
        }
        // The caller releases its recovery pins before collecting these files.
    }

    chunk_registry::repack_stats_t chunk_registry::repack(const repack_mode_t mode, const size_t fragment_threshold)
    {
        // Store-only registries may repack, but publication still needs one writer.
        std::unique_ptr<storage::registry_writer_lock> writer;
        if (!_writer_lock)
            writer = std::make_unique<storage::registry_writer_lock>(_data_dir);
        auto plan = _prepare_repack(mode, fragment_threshold);
        return _commit_repack(*plan);
    }

    std::unique_ptr<chunk_registry::repack_plan_t> chunk_registry::_prepare_repack(const repack_mode_t mode,
        const size_t fragment_threshold) const
    {
        if (_transaction || _journal) [[unlikely]]
            throw error("repack cannot run while a chunk_registry transaction is active!");

        auto plan = std::make_unique<repack_plan_t>();
        plan->stats.chunks_analyzed = _chunks.size();
        plan->stats.compressed_size_before = num_compressed_bytes();
        plan->stats.compressed_size_after = plan->stats.compressed_size_before;
        const auto open_chunk_id = _chunks.empty() ? 0 : make_slot(_chunks.rbegin()->second.last_block.slot).chunk_id();
        const auto tip_height = _chunks.empty() ? 0 : _chunks.rbegin()->second.blocks.back().height;
        const auto recent_floor = tip_height > _cardano_cfg.shelley_security_param
            ? tip_height - _cardano_cfg.shelley_security_param : 0;
        size_t recent_fragments = 0;
        auto logical_end = _chunks.cbegin();
        bool recent = false;
        size_t logical_fragments = 0;
        for (auto begin = _chunks.cbegin(); begin != _chunks.cend();) {
            const auto chunk_id = make_slot(begin->second.first_slot).chunk_id();
            if (begin == logical_end) {
                logical_end = std::next(begin);
                while (logical_end != _chunks.cend() && make_slot(logical_end->second.first_slot).chunk_id() == chunk_id)
                    ++logical_end;
                logical_fragments = static_cast<size_t>(std::distance(begin, logical_end));
                recent = std::prev(logical_end)->second.blocks.back().height >= recent_floor;
            }
            auto end = std::next(begin);
            auto size = begin->second.data_size;
            while (end != logical_end && make_slot(std::prev(end)->second.last_block.slot).chunk_id() == chunk_id
                    && make_slot(end->second.last_block.slot).chunk_id() == chunk_id
                    && end->second.data_size <= zstd::max_zstd_buffer
                    && size <= zstd::max_zstd_buffer - end->second.data_size) {
                size += end->second.data_size;
                ++end;
            }
            const bool merge = std::next(begin) != end;
            const bool selected = mode == repack_mode_t::merge_closed
                ? merge && chunk_id < open_chunk_id
                : mode == repack_mode_t::merge_fragmented
                    ? merge && logical_fragments > fragment_threshold
                    : merge || begin->second.compression_level < zstd::default_compression_level;
            if (selected) {
                const auto key = std::prev(end)->first;
                repack_plan_t::item_t item { .chunk=begin->second, .recent=recent };
                item.sources.reserve(static_cast<size_t>(std::distance(begin, end)));
                for (auto it = begin; it != end; ++it)
                    item.sources.emplace_back(it);
                if (merge && recent)
                    recent_fragments += item.sources.size();
                plan->items.emplace(key, std::move(item));
            }
            begin = end;
        }
        if (mode == repack_mode_t::merge_closed && recent_fragments <= fragment_threshold)
            std::erase_if(plan->items, [](const auto &entry) { return entry.second.recent; });
        if (plan->items.empty())
            return plan;
        // Live publication checks the threshold after every commit. Avoid creating
        // and discarding a journal until there is actual repacking work.
        plan->journal = std::make_unique<storage::commit_journal>(_data_dir);
        plan->directory = plan->journal->stage("scratch");
        for (auto &[key, item]: plan->items)
            item.path = (plan->directory / fmt::format("{}.zstd", key)).string();
        // Remove staging directories left by the pre-journal repacker.
        std::filesystem::remove_all(_db_dir / "repack");
        for (const auto &entry: std::filesystem::directory_iterator { _db_dir })
            if (entry.is_directory() && entry.path().filename().string().starts_with(".repack-"))
                std::filesystem::remove_all(entry.path());
        logger::info("repack started: mode {} output chunks {}",
            mode == repack_mode_t::full ? "full" : mode == repack_mode_t::merge_closed ? "merge-closed" : "merge-fragmented",
            plan->items.size());
        std::filesystem::create_directories(plan->directory);
        std::atomic_size_t completed { 0 };
        const auto prepare = [&](repack_plan_t::item_t &item) {
            const auto &last = item.sources.back()->second;
            const auto size = last.end_offset() - item.chunk.offset;
            if (size > zstd::max_zstd_buffer)
                throw error(fmt::format("repack chunk size {} exceeds the maximum {}", size, zstd::max_zstd_buffer));
            uint8_vector uncompressed(size);
            uint64_t offset = item.chunk.offset;
            for (const auto source_it: item.sources) {
                const auto &source = source_it->second;
                if (source.offset != offset) [[unlikely]]
                    throw error(fmt::format("noncontiguous repack chunk at offset {} instead of {}", source.offset, offset));
                const auto compressed = file::read(full_path(source.rel_path()));
                auto bytes = write_buffer { uncompressed.data() + (offset - item.chunk.offset), source.data_size };
                zstd::decompress(bytes, compressed);
                if (crypto::blake2b::digest<cardano::block_hash>(bytes) != source.data_hash)
                    throw error(fmt::format("cannot repack chunk with incorrect data hash: {}", source.rel_path()));
                offset += source.data_size;
                if (source_it != item.sources.front())
                    item.chunk.blocks.insert(item.chunk.blocks.end(), source.blocks.begin(), source.blocks.end());
            }
            item.chunk.data_size = uncompressed.size();
            item.chunk.num_blocks = item.chunk.blocks.size();
            item.chunk.last_block = last.last_block;
            crypto::blake2b::digest(item.chunk.data_hash, uncompressed);
            // Live fragments are merged frequently, so avoid maximum compression here.
            const auto level = mode == repack_mode_t::merge_fragmented ? 3 : zstd::default_compression_level;
            const auto compressed = zstd::compress(uncompressed, level);
            file::write(item.path, compressed);
            const auto published = trusted_chunk_path(_db_dir, item.chunk);
            item.pin = _file_remover.pin(published);
            _file_remover.unmark(published);
            // Recompression preserves content identity. Publish each finished file
            // immediately so temporary disk use is bounded by active workers.
            // Merged fragments keep their originals until the metadata commits.
            std::filesystem::rename(item.path, published);
            item.chunk.compressed_size = compressed.size();
            item.chunk.compression_level = level;
            if (mode == repack_mode_t::full)
                progress::get().update("repack", ++completed, plan->items.size());
        };
        if (mode != repack_mode_t::full) {
            for (auto &[key, item]: plan->items)
                prepare(item);
        } else {
            scope_exit drain { [&] { logger::run_log_errors([&] { _sched.process(true); }); } };
            int64_t priority = 0;
            for (auto &[key, item]: plan->items)
                _sched.submit("repack", priority--, [&, item_ptr=&item] { prepare(*item_ptr); });
            _sched.process(true);
            drain.release();
        }
        for (const auto &[key, item]: plan->items) {
            ++plan->stats.chunks_repacked;
            plan->stats.partial_groups_merged += item.sources.size() > 1;
            for (const auto source: item.sources)
                plan->stats.compressed_size_after -= source->second.compressed_size;
            plan->stats.compressed_size_after += item.chunk.compressed_size;
        }
        return plan;
    }

    chunk_registry::repack_stats_t chunk_registry::_commit_repack(repack_plan_t &plan)
    {
        if (plan.items.empty())
            return plan.stats;
        chunk_map replacements;
        file_set obsolete_paths;
        for (auto &[key, item]: plan.items) {
            const auto path = full_path(item.chunk.rel_path());
            for (const auto source: item.sources) {
                const auto old_path = full_path(source->second.rel_path());
                if (old_path != path)
                    obsolete_paths.emplace(old_path);
            }

            replacements.emplace(key, std::move(item.chunk));
            _file_remover.unmark(path);
        }
        chunk_map originals;
        for (const auto &[key, item]: plan.items)
            for (const auto source: item.sources)
                originals.insert(_chunks.extract(source));
        scope_exit restore { [&] {
            if (_journal && _journal->decided())
                return; // Publication must be completed on reopen, never rolled back.
            _journal.reset();
            for (const auto &[key, item]: plan.items)
                _chunks.erase(key);
            _chunks.merge(originals);
        } };
        _chunks.merge(replacements);
        _commit_state(std::move(plan.journal));
        restore.release();
        logger::run_log_errors([&] {
            logger::info(
                "repack complete: analyzed {} chunks, repacked {}, merged {} partial groups, compressed size {} -> {} bytes",
                plan.stats.chunks_analyzed, plan.stats.chunks_repacked, plan.stats.partial_groups_merged,
                plan.stats.compressed_size_before, plan.stats.compressed_size_after);
            for (const auto &path: obsolete_paths)
                _file_remover.mark(path);
            _file_remover.remove();
        });
        return plan.stats;
    }

    void chunk_registry::validation_failure_handler(const std::function<void(uint64_t)> &handler)
    {
        static const std::vector<std::string> task_names{
            std::string{validator::validate_leaders_task},
            std::string{validator::validate_task}
        };
        const auto internal_handler = [handler](const scheduled_task_error &err) {
            const chunk_offset_t *offset_ptr = err.task().param ? std::any_cast<chunk_offset_t>(&*err.task().param) : nullptr;
            const auto offset = offset_ptr ? *offset_ptr : 0U;
            logger::debug("chunk_registry::validation_failure_handler at offset {}: {}", offset, err.what());
            handler(offset);
        };
        for (const auto &task: task_names)
            _sched.on_error(task, internal_handler, true);
    }

    const indexer::incremental &chunk_registry::indexer() const
    {
        if (_indexer) [[likely]]
            return *_indexer;
        throw error("This chunk_registry does not have an indexer instance!");
    }

    const validator::incremental &chunk_registry::validator() const
    {
        if (_validator) [[likely]]
            return *_validator;
        throw error("This chunk_registry does not have a validator instance!");
    }

    cardano::amount chunk_registry::unspent_reward(const cardano::stake_ident &id) const
    {
        if (_validator) [[likely]]
            return _validator->unspent_reward(id);
        throw error("This chunk_registry does not have a validator instance!");
    }

    cardano::optional_point chunk_registry::tip() const
    {
        if (const auto last_block = last_valid_block(); last_block) [[likely]]
            return cardano::point { last_block->hash, last_block->slot, last_block->height, last_block->end_offset() };
        return {};
    }

    cardano::optional_point chunk_registry::core_tip() const
    {
        return validator().core_tip();
    }

    cardano::optional_point chunk_registry::immutable_tip() const
    {
        if (!_chunks.empty()) {
            size_t blocks_after = 0;
            for (const auto &[last_byte, chunk]: _chunks | std::ranges::views::reverse) {
                if (blocks_after >= _cardano_cfg.shelley_security_param)
                    return chunk.blocks.back().point();
                blocks_after += chunk.num_blocks;
            }
        }
        return {};
    }

    std::optional<chunk_registry::active_transaction> chunk_registry::tx() const
    {
        return _transaction;
    }

    const cardano::config &chunk_registry::config() const
    {
        return _cardano_cfg;
    }

    scheduler &chunk_registry::sched() const
    {
        return _sched;
    }

    file_remover &chunk_registry::remover() const
    {
        return _file_remover;
    }

    bool chunk_registry::empty() const
    {
        return cbegin() == cend();
    }

    chunk_registry::const_iterator chunk_registry::cbegin() const
    {
        return const_iterator::cbegin(*this, _chunks);
    }

    chunk_registry::const_iterator chunk_registry::cend() const
    {
        return const_iterator::cend(*this, _chunks);
    }

    chunk_registry::const_reverse_iterator chunk_registry::crbegin() const
    {
        return { cend() };
    }

    chunk_registry::const_reverse_iterator chunk_registry::crend() const
    {
        return { cbegin() };
    }

    const chunk_registry::chunk_map &chunk_registry::chunks() const
    {
        return _chunks;
    }

    epoch_map chunk_registry::epochs() const
    {
        mutex::scoped_lock lk { _update_mutex };
        epoch_map eps {};
        std::optional<uint64_t> last_epoch {};
        epoch_info::chunk_list chunks {};
        for (const auto &[last_byte_offset, chunk]: _chunks) {
            const auto chunk_epoch = make_slot(chunk.first_slot).epoch();
            if (!last_epoch || *last_epoch != chunk_epoch) {
                if (last_epoch && !chunks.empty())
                    eps.try_emplace(*last_epoch, std::move(chunks));
                last_epoch = chunk_epoch;
                chunks.clear();
            }
            chunks.emplace_back(&chunk);
        }
        if (last_epoch && !chunks.empty())
            eps.try_emplace(*last_epoch, std::move(chunks));
        return eps;
    }

    bool chunk_registry::has_epoch(const uint64_t epoch) const
    {
        mutex::unique_lock lk { _update_mutex };
        return _has_epoch(epoch, lk);
    }

    cardano::slot chunk_registry::make_slot(uint64_t slot_) const
    {
        return { slot_, _cardano_cfg };
    }

    uint64_t chunk_registry::num_bytes() const
    {
        if (!_chunks.empty()) [[likely]]
            return _chunks.rbegin()->second.end_offset();
        return 0;
    }

    uint64_t chunk_registry::num_compressed_bytes() const
    {
        if (!_chunks.empty()) [[likely]]
            return std::accumulate(_chunks.begin(), _chunks.end(), 0ULL,
                [](auto sum, const auto &chunk) { return sum + chunk.second.compressed_size; });
        return 0;
    }

    size_t chunk_registry::num_blocks() const
    {
        return std::accumulate(_chunks.begin(), _chunks.end(), static_cast<size_t>(0),
            [](auto sum, const auto &val) { return sum + val.second.blocks.size(); });
    }

    chunk_registry::const_iterator chunk_registry::find_by_offset(const uint64_t offset) const
    {
        if (const auto chunk_it = _find_chunk_by_offset(offset); chunk_it != _chunks.end()) {
            if (const auto block_it = _find_block_by_offset(chunk_it, offset); block_it != chunk_it->second.blocks.end()) {
                if (offset >= block_it->offset && offset < block_it->offset + block_it->size)
                    return { *this, _chunks, chunk_it, numeric_cast<size_t>(block_it - chunk_it->second.blocks.begin()) };
                throw error("internal error: block metadata does not match the transaction!");
            }
        }
        return const_iterator::cend(*this, _chunks);
    }

    std::optional<storage::block_info> chunk_registry::find_block_by_offset_no_throw(const uint64_t offset) const
    {
        if (const auto it = find_by_offset(offset); it != cend())
            return *it;
        return {};
    }

    storage::block_info chunk_registry::find_block_by_offset(const uint64_t offset) const
    {
        if (const auto block = find_block_by_offset_no_throw(offset))
            return *block;
        throw error(fmt::format("unknown offset: {}!", offset));
    }

    const storage::block_info &chunk_registry::find_block_by_slot(const uint64_t slot) const
    {
        const auto chunk_it = _find_chunk_by_slot(slot);
        if (chunk_it == _chunks.end()) [[unlikely]]
            throw error(fmt::format("internal error: no block registered at a slot: {}!", slot));
        const auto block_it = _find_block_by_slot(chunk_it, slot);
        if (block_it == chunk_it->second.blocks.end()) [[unlikely]]
            throw error(fmt::format("internal error: no block registered at a slot: {}!", slot));
        if (block_it->slot != slot) [[unlikely]]
            throw error(fmt::format("internal error: no block registered at a slot: {}!", slot));
        return *block_it;
    }

    chunk_registry::const_iterator chunk_registry::find_block(const cardano::point2 &p) const
    {
        if (auto chunk_it = _find_chunk_by_slot(p.slot); chunk_it != _chunks.end()) [[likely]] {
            if (auto block_it = _find_block_by_slot(chunk_it, p.slot); block_it != chunk_it->second.blocks.end()) [[likely]] {
                if (block_it->slot == p.slot) {
                    for (;;) {
                        if (block_it->hash == p.hash) [[likely]]
                            return { *this, _chunks, chunk_it, numeric_cast<size_t>(block_it - chunk_it->second.blocks.begin()) };
                        if (++block_it == chunk_it->second.blocks.end()) [[unlikely]] {
                            if (++chunk_it == _chunks.end()) [[unlikely]]
                                break;
                            block_it = chunk_it->second.blocks.begin();
                        }
                        if (block_it->slot != p.slot)
                            break;
                    }
                }
            }
        }
        return cend();
    }

    storage::block_info chunk_registry::get_block_info(const cardano::point2 &p) const
    {
        if (const auto block_it = find_block(p); block_it != cend()) [[likely]]
            return *block_it;
        throw error(fmt::format("internal error: no such block: {}!", p));
    }

    uint64_t chunk_registry::find_epoch(const uint64_t offset) const
    {
        mutex::scoped_lock lk { _update_mutex };
        return make_slot(find_offset(offset).first_slot).epoch();
    }

    const chunk_registry::chunk_info &chunk_registry::find_offset(uint64_t offset) const
    {
        return _find_chunk_by_offset(offset)->second;
    }

    const chunk_registry::chunk_info &chunk_registry::find_last_block_hash(const buffer &last_block_hash) const
    {
        const auto it = std::find_if(_chunks.begin(), _chunks.end(),
                                     [&](const auto &el) { return el.second.last_block.hash == last_block_hash; });
        if (it == _chunks.end()) [[unlikely]]
            throw error(fmt::format("there is no chunk with its last block hash {}", last_block_hash));
        return it->second;
    }

    chunk_registry::chunk_map::const_iterator chunk_registry::find_offset_it(uint64_t offset) const
    {
        return _find_chunk_by_offset(offset);
    }

    std::optional<storage::block_info> chunk_registry::last_valid_block() const
    {
        const auto end_offset = valid_end_offset();
        if (end_offset) [[likely]] {
            const auto chunk_it = _find_chunk_by_offset(end_offset - 1);
            if (chunk_it == _chunks.end()) [[unlikely]]
                throw error("internal error: chunk_registry state is inconsistent!");
            const auto block_it = _find_block_by_offset(chunk_it, end_offset - 1);
            if (block_it == chunk_it->second.blocks.end()) [[unlikely]]
                throw error("internal error: chunk_registry state is inconsistent!");
            return *block_it;
        }
        return {};
    }

    uint64_t chunk_registry::max_slot() const
    {
        if (!_chunks.empty()) [[likely]]
            return _chunks.rbegin()->second.last_block.slot;
        return 0;
    }

    uint64_t chunk_registry::valid_end_offset() const
    {
        uint64_t valid_end = _my_end_offset();
        for (const auto *p: _processors) {
            if (p->end_offset) {
                const auto proc_end = p->end_offset();
                if (proc_end < valid_end)
                    valid_end = proc_end;
            }
        }
        return valid_end;
    }

    uint64_t chunk_registry::max_end_offset() const
    {
        uint64_t max_end = _my_end_offset();
        for (const auto *p: _processors) {
            if (p->end_offset) {
                const auto proc_end = p->end_offset();
                if (proc_end > max_end)
                    max_end = proc_end;
            }
        }
        return max_end;
    }

    // block data access

    const std::filesystem::path &chunk_registry::data_dir() const
    {
        return _data_dir;
    }

    std::string chunk_registry::rel_path(const std::filesystem::path &full_path) const
    {
        return const_iterator::rel_path(_db_dir, full_path);
    }

    std::string chunk_registry::stage_path(const std::filesystem::path &relative) const
    {
        if (!_journal)
            throw error("staging requires an active transaction");
        return _journal->stage(relative).string();
    }

    std::string chunk_registry::read_path(const std::filesystem::path &relative) const
    {
        return (_journal ? _journal->read(relative) : _data_dir / relative).make_preferred().string();
    }

    void chunk_registry::stage_output(const std::filesystem::path &relative)
    {
        if (!_journal)
            throw error("publishing an output requires an active transaction");
        _file_remover.unmark((_data_dir / relative).make_preferred().string());
        _journal->add(relative);
    }

    std::string chunk_registry::full_path(const std::filesystem::path &rel_path) const
    {
        if (rel_path.empty() || rel_path.has_root_path()
                || std::ranges::any_of(rel_path, [](const auto &part) { return part == ".."; })) [[unlikely]]
            throw error("chunk path '{}' must be relative to '{}' without '..' components", rel_path, _db_dir);
        return read_path(std::filesystem::path { "compressed" } / rel_path);
    }

    uint64_t chunk_registry::read_holding_chunk(uint8_vector &chunk_data, const uint64_t offset) const
    {
        if (offset >= num_bytes()) [[unlikely]]
            throw error(fmt::format("the requested offset {} is larger than the maximum one: {}", offset, num_bytes()));
        const auto &chunk = find_offset(offset);
        if (offset >= chunk.offset + chunk.data_size) [[unlikely]]
            throw error("the requested chunk segment is too small to parse it");
        file::read_auto(full_path(chunk.rel_path()), chunk_data);
        return chunk.offset;
    }

    cbor::zero2::parsed_value chunk_registry::read_from_chunk_buffer(const uint64_t value_offset, const buffer &chunk_data, const uint64_t chunk_offset) const
    {
        if (value_offset < chunk_offset) [[unlikely]]
            throw error("the requested value offset is outside of the chunk's data range!");
        if (value_offset >= chunk_offset + chunk_data.size()) [[unlikely]]
            throw error("the requested chunk segment is too small to parse it");
        const size_t read_offset = value_offset - chunk_offset;
        const size_t read_size = chunk_data.size() - read_offset;
        return cbor::zero2::parse(chunk_data.subbuf(read_offset, read_size));
    }

    cbor::zero2::parsed_value chunk_registry::read(const uint64_t offset) const
    {
        const auto chunk_offset = read_holding_chunk(_read_buffer, offset);;
        return read_from_chunk_buffer(offset, _read_buffer, chunk_offset);
    }

    // state modifying methods

    void chunk_registry::import(const chunk_registry &src_cr)
    {
        if (src_cr.empty())
            return;
        const auto target = src_cr.tip();
        if (!target)
            throw error("cannot import nonempty registry '{}' without a validated tip", src_cr.data_dir());
        accept_anything_or_throw(tip(), *target, [&] {
            for (const auto &[last_byte_offset, src_chunk]: src_cr._chunks) {
                const auto src_path = src_cr.full_path(src_chunk.rel_path());
                add_file(src_chunk.offset, src_path, src_chunk.compression_level);
            }
        });
    }

    progress_point chunk_registry::add_buffer(const uint64_t offset, uint8_vector uncompressed, const int32_t compression_level)
    {
        const auto compressed = zstd::compress(uncompressed, compression_level);
        return add_buffer_trusted(offset, uncompressed, compressed, compression_level);
    }

    progress_point chunk_registry::add_compressed(const uint64_t offset, uint8_vector compressed, const int32_t compression_level)
    {
        const auto uncompressed = zstd::decompress(compressed);
        return add_buffer_trusted(offset, uncompressed, compressed, compression_level);
    }

    progress_point chunk_registry::add_buffer_trusted(const uint64_t offset, const buffer uncompressed,
        const buffer compressed, const int32_t compression_level)
    {
        if (!_transaction) [[unlikely]] {
            throw error("add can be executed only inside of a transaction!");
        }
        if (uncompressed.empty() || uncompressed.size() > zstd::max_zstd_buffer) [[unlikely]] {
            throw error(fmt::format("invalid chunk size: {}", uncompressed.size()));
        }
        const auto data_hash = crypto::blake2b::digest<cardano::block_hash>(uncompressed);
        const auto rel_path = fmt::format("chunk/{}.zstd.tmp", data_hash);
        const auto local_path = stage_path(std::filesystem::path { "compressed" } / rel_path);
        // The journal owns both this output and any failed-write temporary file.
        file::write(local_path, compressed);
        return _add(offset, local_path, uncompressed, compressed.size(), compression_level, data_hash);
    }

    void chunk_registry::add_file(const uint64_t offset, const std::string &local_path, const int32_t compression_level)
    {
        const auto compressed = file::read(local_path);
        const auto uncompressed = zstd::decompress(compressed);
        _add(offset, local_path, uncompressed, compressed.size(), compression_level);
    }

    progress_point chunk_registry::_add(const uint64_t offset, const std::string &local_path,
        const buffer uncompressed, const uint64_t compressed_size, const int32_t compression_level,
        std::optional<cardano::block_hash> data_hash)
    {
        // TODO: add a fast path for data beyond earliest known invalid offset
        if (!_transaction) [[unlikely]] {
            throw error("add can be executed only inside of a transaction!");
        }
        if (uncompressed.empty() || uncompressed.size() > zstd::max_zstd_buffer) [[unlikely]] {
            throw error(fmt::format("invalid chunk size: {}", uncompressed.size()));
        }
        auto [parsed_chunk, ex_ptr] = _parse(offset, uncompressed, compressed_size, compression_level, data_hash);
        const progress_point parsed_progress { parsed_chunk.last_block.slot, parsed_chunk.end_offset() };
        const auto final_path = stage_path(std::filesystem::path { "compressed" } / parsed_chunk.rel_path());
        if (!parsed_chunk.blocks.empty()) {
            const auto live_path = trusted_chunk_path(_db_dir, parsed_chunk);
            if (!ex_ptr && local_path != final_path && std::filesystem::path { local_path } != std::filesystem::path { live_path }) {
                // Move our own temporary output; preserve external import inputs.
                if (std::filesystem::path { local_path }.parent_path() == std::filesystem::path { final_path }.parent_path())
                    std::filesystem::rename(local_path, final_path);
                else
                    std::filesystem::copy_file(local_path, final_path, std::filesystem::copy_options::overwrite_existing);
            }
            _add(std::move(parsed_chunk));
        }
        if (ex_ptr) [[unlikely]] {
            std::rethrow_exception(ex_ptr);
        }
        return parsed_progress;
    }

    [[nodiscard]] std::exception_ptr chunk_registry::accept_progress(const cardano::optional_point &start, const progress_point &target, const std::function<void()> &action)
    {
        return _accept_progress(start, target, true, action);
    }

    void chunk_registry::accept_anything_or_throw(const cardano::optional_point &start, const progress_point &target, const std::function<void()> &action)
    {
        if (const auto ex_ptr = _accept_progress(start, target, false, action); ex_ptr)
            std::rethrow_exception(ex_ptr);
    }

    void chunk_registry::truncate(const cardano::optional_point &new_tip)
    {
        if (const auto ex_ptr = _accept_progress(new_tip, new_tip, false, []{}); ex_ptr)
            std::rethrow_exception(ex_ptr);
    }

    std::string chunk_registry::node_export_ledger(const std::filesystem::path &ledger_dir, const cardano::optional_point &imm_tip, const int prio) const
    {
        if (_validator) [[likely]] {
            if (imm_tip && _validator->can_export(imm_tip)) {
                std::filesystem::create_directories(ledger_dir);
                return _validator->node_export(ledger_dir, imm_tip, prio);
            }
            throw error("no Shelley-or-later ledger snapshot is available at or before the immutable tip");
        }
        throw error("This chunk_registry does not have a validator instance!");
    }

    chunk_registry::const_iterator chunk_registry::latest_block_after_or_at_slot(const uint64_t slot) const
    {
        for (auto chunk_it = _find_chunk_by_slot(slot); chunk_it != _chunks.end(); ++chunk_it) {
            for (auto block_it = chunk_it->second.blocks.begin(); block_it != chunk_it->second.blocks.end(); ++block_it) {
                if (block_it->slot >= slot)
                    return { *this, _chunks, chunk_it, numeric_cast<size_t>(block_it - chunk_it->second.blocks.begin()) };
            }
        }
        return cend();
    }

    chunk_registry::const_iterator chunk_registry::latest_block_before_or_at_slot(const uint64_t slot) const
    {
        if (!_chunks.empty()) {
            auto chunk_it = _find_chunk_by_slot(slot);
            if (chunk_it == _chunks.end())
                --chunk_it;
            for (;;) {
                for (auto block_it = chunk_it->second.blocks.rbegin(); block_it != chunk_it->second.blocks.rend(); ++block_it) {
                    if (block_it->slot <= slot)
                        return { *this, _chunks, chunk_it, numeric_cast<size_t>(numeric_cast<ptrdiff_t>(chunk_it->second.blocks.size()) - (block_it - chunk_it->second.blocks.rbegin() + 1)) };;
                }
                if (chunk_it == _chunks.begin())
                    break;
                --chunk_it;
            }
        }
        return cend();
    }

    void chunk_registry::_node_export_chain(const std::filesystem::path &immutable_dir, const std::filesystem::path &volatile_dir, const int prio_base) const
    {
        // chunk registry may store the same Cardano Node chunk in multiple files, so need to combine them for the export
        struct merged_chunk {
            uint64_t first_slot = 0;
            uint64_t last_slot = 0;
            std::vector<std::string> files {};
            std::vector<const storage::block_info *> blocks {};
        };
        using merged_chunk_map = std::map<uint64_t, merged_chunk>;

        std::filesystem::remove_all(immutable_dir);
        std::filesystem::create_directories(immutable_dir);
        const auto done_bytes = std::make_shared<std::atomic_uint64_t>(0);
        const auto total_bytes = num_bytes();

        // split chunks into volatile and immutable ones
        std::vector<const storage::chunk_info *> volatile_chunks {};
        merged_chunk_map immutable_chunks {};
        const auto imm_tip = immutable_tip();
        for (const auto &[last_byte, chunk]: _chunks) {
            if (imm_tip < chunk.blocks.back().point()) {
                volatile_chunks.emplace_back(&chunk);
            } else {
                const auto chunk_id = make_slot(chunk.first_slot).chunk_id();
                const auto chunk_path = full_path(chunk.rel_path());
                const auto [it, created] = immutable_chunks.try_emplace(chunk_id, chunk.first_slot);
                it->second.last_slot = chunk.last_block.slot;
                it->second.files.emplace_back(chunk_path);
                for (const auto &block: chunk.blocks)
                    it->second.blocks.emplace_back(&block);
            }
        }

        logger::info("exporting chunks to {} immutable: {} volatile: {}", immutable_dir.string(), immutable_chunks.size(), volatile_chunks.size());
        // export immutable chunks
        for (const auto &[chunk_id, m_chunk]: immutable_chunks) {
            _sched.submit("decompress", prio_base, [this, done_bytes, total_bytes, chunk_id, m_chunk, immutable_dir, imm_tip] {
                const auto data_path = (immutable_dir / fmt::format("{:05}.chunk", chunk_id)).string();
                const auto pri_path = (immutable_dir / fmt::format("{:05}.primary", chunk_id)).string();
                const auto sec_path = (immutable_dir / fmt::format("{:05}.secondary", chunk_id)).string();
                const auto chunk_start_slot = cardano::slot::from_chunk(chunk_id, _cardano_cfg);
                const uint64_t chunk_start_offset = m_chunk.blocks.front()->offset;
                uint64_t chunk_max_slot = _cardano_cfg.byron_slots_per_chunk;
                if (imm_tip && imm_tip->slot - chunk_start_slot < chunk_max_slot)
                    chunk_max_slot = imm_tip->slot - chunk_start_slot;
                uint64_t data_size = 0;
                {
                    logger::debug("writing chunk {}", data_path);
                    uint8_vector data {};
                    for (const auto &path: m_chunk.files)
                        data << file::read_auto(path);
                    file::write(data_path, data);
                    data_size = data.size();
                }
                {
                    file::write_stream pri_ws { pri_path };
                    file::write_stream sec_ws { sec_path };
                    pri_ws.write(buffer::from<uint8_t>(1));
                    uint32_t next_block_offset = 0;
                    uint32_t next_rel_slot = 0;
                    for (const auto *blk: m_chunk.blocks) {
                        if (blk->slot < chunk_start_slot) [[unlikely]]
                            throw error(fmt::format("block with slot {} must not be in chunk {}!", blk->slot, chunk_id));
                        const auto blk_rel_slot = blk->era > 0 ? blk->slot - chunk_start_slot + 1 : 0;
                        for (; next_rel_slot <= blk_rel_slot; ++next_rel_slot)
                            pri_ws.write(buffer::from(host_to_net<uint32_t>(next_block_offset)));
                        if (blk->offset < chunk_start_offset) [[unlikely]]
                            throw error(fmt::format("block with offset {} must not be in chunk starting at offset {}!", blk->offset, chunk_start_offset));
                        const auto blk_rel_offset = blk->offset - chunk_start_offset;
                        sec_ws.write(buffer::from(host_to_net<uint64_t>(blk_rel_offset)));
                        sec_ws.write(buffer::from(host_to_net<uint16_t>(blk->header_offset)));
                        sec_ws.write(buffer::from(host_to_net<uint16_t>(blk->header_size)));
                        sec_ws.write(buffer::from(host_to_net<uint32_t>(blk->chk_sum)));
                        sec_ws.write(blk->hash);
                        //store 0 instead of blk.height for byte-for-byte compatibility with Cardano Node
                        sec_ws.write(buffer::from(host_to_net<uint32_t>(0)));
                        sec_ws.write(buffer::from(host_to_net<uint32_t>(blk->era > 0 ? blk->slot : chunk_start_slot.epoch())));
                        next_block_offset += 56;
                        next_rel_slot = blk_rel_slot + 1;
                    }
                    for (; next_rel_slot <= chunk_max_slot; ++next_rel_slot)
                        pri_ws.write(buffer::from(host_to_net<uint32_t>(next_block_offset)));
                    pri_ws.write(buffer::from(host_to_net<uint32_t>(next_block_offset)));
                }
                const auto new_done_blocks = done_bytes->fetch_add(data_size, std::memory_order_relaxed) + data_size;
                progress::get().update("chunk-export", new_done_blocks, total_bytes);
            });
        }

        // export volatile chunks
        {
            std::filesystem::remove_all(volatile_dir);
            std::filesystem::create_directories(volatile_dir);
            static constexpr size_t max_volatile_file_blocks = 1000;
            uint8_vector volatile_data {};
            std::vector<size_t> volatile_block_sizes {};
            for (const auto *chunk_ptr: volatile_chunks) {
                volatile_data << file::read_auto(full_path(chunk_ptr->rel_path()));
                for (const auto &block: chunk_ptr->blocks)
                    volatile_block_sizes.emplace_back(block.size);
            }
            uint64_t volatile_offset = 0;
            uint64_t volatile_file_no = 0;
            for (size_t bi = 0; bi < volatile_block_sizes.size(); bi += max_volatile_file_blocks) {
                const uint64_t start_offset = volatile_offset;
                uint64_t file_size = 0;
                const auto batch_end = std::min(volatile_block_sizes.size(), bi + max_volatile_file_blocks);
                for (size_t i = bi; i < batch_end; ++i) {
                    file_size += volatile_block_sizes[i];
                }
                file::write(
                    (volatile_dir / fmt::format("blocks-{}.dat", volatile_file_no)).string(),
                    static_cast<buffer>(volatile_data).subbuf(start_offset, file_size));
                volatile_offset += file_size;
                ++volatile_file_no;
                const auto new_done_blocks = done_bytes->fetch_add(file_size, std::memory_order_relaxed) + file_size;
                progress::get().update("chunk-export", new_done_blocks, total_bytes);
            }
        }
    }

    void chunk_registry::node_export(const std::filesystem::path &node_dir, const cardano::point &tip, const bool ledger_only) const
    {
        progress_guard pg { "chunk-export", "ledger-export" };
        logger::debug("node_export started to {}", node_dir.string());
        const auto ex_ptr = logger::run_log_errors([&] {
            node_export_ledger(std::filesystem::weakly_canonical(node_dir / "ledger"), tip);
            if (!ledger_only) {
                std::filesystem::remove(node_dir / "clean");
                _node_export_chain(std::filesystem::weakly_canonical(node_dir / "immutable").string(),
                    std::filesystem::weakly_canonical(node_dir / "volatile").string(), 100);
                std::filesystem::remove(node_dir / "lock");
                file::write((node_dir / "protocolMagicId").string(), fmt::format("{}", _cardano_cfg.byron_protocol_magic));
                file::write((node_dir / "clean").string(), std::string_view { "" });
            }
        });
        if (ex_ptr)
            _sched.cancel([](const auto &, const auto &) { return true; });
        _sched.process(true);
        if (ex_ptr)
            std::rethrow_exception(ex_ptr);
    }

    cardano::optional_slot chunk_registry::can_export() const
    {
        return validator().can_export(immutable_tip());
    }

    void chunk_registry::_add(chunk_info &&chunk, const bool normal)
    {
        try {
            if (normal && _transaction->target_slot() < chunk.last_block.slot) [[unlikely]]
                throw error(fmt::format("chunk's slot range {}:{} exceeds the target slot: {}",
                    chunk.first_slot, chunk.last_block.slot, _transaction->target_slot()));
            if (chunk.data_size == 0 || chunk.num_blocks == 0 || chunk.blocks.empty()) [[unlikely]]
                throw error(fmt::format("chunk at offset {} is empty!", chunk.offset));
            mutex::unique_lock update_lk { _update_mutex };
            auto [um_it, um_created] = _unmerged_chunks.try_emplace(chunk.offset + chunk.data_size - 1, std::move(chunk));
            // chunk variable should not be used after this point due to std::move(chunk) right above
            if (!um_created) [[unlikely]]
                throw error(fmt::format("internal error: duplicate chunk offset: {} size: {}", um_it->second.offset, um_it->second.data_size));
            while (!_unmerged_chunks.empty() && _unmerged_chunks.begin()->second.offset == num_bytes()) {
                const auto &tested_chunk = _unmerged_chunks.begin()->second;
                if (const auto &first_block = tested_chunk.blocks.at(0); first_block.era >= 2 && !_cardano_cfg.shelley_started()) {
                    // If there were no blocks before this one, then count from the slot 0
                    _cardano_cfg.shelley_start_epoch(_chunks.empty() ? 0 : first_block.slot / _cardano_cfg.byron_epoch_length);
                }
                if (_validator) {
                    if (const auto future_slot = cardano::slot::from_future(_cardano_cfg); tested_chunk.last_block.slot >= future_slot) [[unlikely]]
                        throw error(fmt::format("a chunk with its last block with a time slot from the future: {}!", tested_chunk.last_block.slot));
                    if (!_chunks.empty()) {
                        const auto &last = _chunks.rbegin()->second;
                        require_block_successor(last.last_block, last.blocks.back().era,
                            tested_chunk.prev_block_hash, tested_chunk.first_slot, tested_chunk.blocks.front().era);
                    } else {
                        if (tested_chunk.prev_block_hash != _cardano_cfg.byron_genesis_hash) [[unlikely]]
                            throw error(fmt::format("chunk at offset {}: prev_block_hash {} does not match the genesis hash {}",
                                tested_chunk.offset, tested_chunk.prev_block_hash, _cardano_cfg.byron_genesis_hash));
                    }
                }
                const auto first_slot = make_slot(tested_chunk.first_slot);
                const auto last_slot = make_slot(tested_chunk.last_block.slot);
                if (first_slot.epoch() != last_slot.epoch()) [[unlikely]]
                    throw error(fmt::format("chunk at offset {} contains blocks from multiple epochs: first slot: {} last_slot: {}", tested_chunk.offset, first_slot, last_slot));
                if (first_slot.chunk_id() != last_slot.chunk_id()) [[unlikely]]
                    throw error(fmt::format("chunk at offset {} contains blocks from multiple chunks: {} and {}", tested_chunk.offset, first_slot.chunk_id(), last_slot.chunk_id()));
                auto [it, created, node] = _chunks.insert(_unmerged_chunks.extract(_unmerged_chunks.begin()));
                const auto &inserted_chunk = it->second;
                if (!created) [[unlikely]]
                    throw error(fmt::format("internal error: duplicate chunk offset: {} size: {}", inserted_chunk.offset, inserted_chunk.data_size));
            }
            if (normal) {
                _notify_of_updates(update_lk);
                logger::debug("chunk_registry::_add: first_slot: {} last_slot: {} -> SUCCESS", make_slot(chunk.first_slot), make_slot(chunk.last_block.slot));
            }
        } catch (...) {
            logger::debug("chunk_registry::_add: first_slot: {} last_slot: {} -> FAILURE", make_slot(chunk.first_slot), make_slot(chunk.last_block.slot));
            if (normal || _mode != mode::validate) [[unlikely]] {
                throw;
            }
        }
    }

    std::pair<storage::chunk_info, std::exception_ptr> chunk_registry::_parse(const uint64_t offset,
        const buffer &raw_data, const size_t compressed_size, const int32_t compression_level,
        const std::optional<cardano::block_hash> &data_hash) const
    {
        std::exception_ptr ex_ptr{};
        std::optional<indexer::chunk_indexer_list_t> chunk_indexers{};
        if (_indexer)
            chunk_indexers = _indexer->make_chunk_indexers(offset);
        chunk_info chunk {
            .offset=offset,
            .data_size=raw_data.size(),
            .compressed_size=compressed_size,
            .compression_level=compression_level
        };
        size_t valid_data_size = 0;
        storage::block_reader_t reader { raw_data, offset, _cardano_cfg };
        while (!reader.done()) {
            try {
                const auto blk_ptr = reader.next();
                {
                    const auto &blk = *blk_ptr;
                    const auto slot = blk.slot();
                    if (!chunk.blocks.empty()) {
                        require_block_successor(chunk.last_block, chunk.blocks.back().era,
                            blk.prev_hash(), slot, blk.era(), static_cast<bool>(_validator));
                    }
                    static constexpr auto max_era = std::numeric_limits<uint8_t>::max();
                    if (blk.era() > max_era) [[unlikely]]
                        throw error(fmt::format("block at slot {} has era {} that is outside of the supported max limit of {}", slot, blk.era(), max_era));
                    static constexpr auto max_size = std::numeric_limits<uint32_t>::max();
                    if (blk_ptr.raw().size() > max_size) [[unlikely]]
                        throw error(fmt::format("block at slot {} has size {} that is outside of the supported max limit of {}", slot, blk_ptr.raw().size(), max_size));
                    if (!chunk.blocks.empty()) {
                        const auto prev_chunk_id = cardano::slot::chunk_id(chunk.last_block.slot, _cardano_cfg);
                        const auto next_chunk_id = cardano::slot::chunk_id(slot, _cardano_cfg);
                        if (prev_chunk_id != next_chunk_id) [[unlikely]]
                            throw error(fmt::format("chunk at offset {} contains blocks from multiple chunks: {} and {}", offset, prev_chunk_id, next_chunk_id));
                    } else {
                        chunk.prev_block_hash = blk.prev_hash();
                        chunk.first_slot = slot;
                    }
                    for (const auto *p: _processors) {
                        if (p->on_block_validate)
                            p->on_block_validate(blk);
                    }
                    chunk.last_block = { slot, blk.hash() };
                    if (chunk_indexers) {
                        chunk_indexers->index_block(blk_ptr);
                    }
                    chunk.blocks.emplace_back(storage::block_info::from_block(blk_ptr));
                    valid_data_size = numeric_cast<size_t>(blk_ptr.end_offset() - chunk.offset);
                }
            } catch (...) {
                ex_ptr = std::current_exception();
                break;
            }
        }

        // The decoder consumes consecutive top-level values, so successfully processed blocks
        // always form a prefix of raw_data, including when a later block is invalid.
        const auto valid_data = raw_data.subbuf(0, valid_data_size);
        if (data_hash && valid_data.size() == raw_data.size())
            chunk.data_hash = *data_hash;
        else
            crypto::blake2b::digest(chunk.data_hash, valid_data);
        chunk.num_blocks = chunk.blocks.size();
        if (valid_data.size() != raw_data.size()) {
            chunk.data_size = valid_data.size();
            if (!valid_data.empty()) {
                const auto compressed = zstd::compress(valid_data);
                chunk.compressed_size = compressed.size();
                chunk.compression_level = zstd::default_compression_level;
                file::write(stage_path(std::filesystem::path { "compressed" } / chunk.rel_path()), compressed);
            }
        }
        for (const auto *p: _processors) {
            if (p->on_chunk_add)
                p->on_chunk_add(chunk);
            if (p->on_chunk_data && !chunk.blocks.empty())
                p->on_chunk_data(chunk, valid_data);
        }
        // chunks can be parsed out of order so in the end offset we report the number of parsed bytes
        // rather than the last parsed offset as this better reflects the progress made
        const auto num_parsed = _tx_progress_parse.fetch_add(chunk.data_size, std::memory_order_relaxed) + chunk.data_size;
        report_progress("parse", { chunk.last_block.slot,  _transaction->start_offset() + num_parsed });
        return std::make_pair(std::move(chunk), std::move(ex_ptr));
    }

    epoch_info chunk_registry::_epoch(const uint64_t epoch) const
    {
        epoch_info::chunk_list chunks {};
        auto chunk_it = std::lower_bound(_chunks.begin(), _chunks.end(), epoch,
            [this](const auto &el, const auto &epoch) { return make_slot(el.second.first_slot).epoch() < epoch; });
        for (; chunk_it != _chunks.end() && make_slot(chunk_it->second.first_slot).epoch() == epoch; ++chunk_it) {
            chunks.emplace_back(&chunk_it->second);
        }
        return { std::move(chunks) };
    }

    void chunk_registry::_my_truncate(const cardano::optional_point &new_tip, const bool track_changes)
    {
        if (const auto max_end_offset = new_tip ? new_tip->end_offset : 0; max_end_offset < num_bytes()) {
            timer t { fmt::format("chunk_registry::_truncate to {}", new_tip), logger::level::info };
            auto chunk_it = _find_chunk_by_offset(max_end_offset);
            if (chunk_it->second.offset < max_end_offset) {
                auto block_it = _find_block_by_offset(chunk_it, max_end_offset);
                if (block_it == chunk_it->second.blocks.end()) [[unlikely]]
                    throw error(fmt::format("internal error: no block covers offset {}", max_end_offset));
                // truncate chunk data and update its metadata
                if (track_changes)
                    _truncated_chunks.emplace(chunk_it->first, chunk_it->second);
                auto next_chunk_it = std::next(chunk_it);
                auto node = _chunks.extract(chunk_it);
                auto &chunk = node.mapped();
                chunk.blocks.resize(block_it - chunk.blocks.begin());
                chunk.num_blocks = chunk.blocks.size();
                chunk.data_size = chunk.blocks.back().end_offset() - chunk.offset;
                const auto old_path = full_path(chunk.rel_path());
                auto chunk_data = file::read_auto(old_path);
                chunk_data.resize(chunk.data_size);
                crypto::blake2b::digest(chunk.data_hash, chunk_data);
                const auto compressed = zstd::compress(chunk_data);
                file::write(stage_path(std::filesystem::path { "compressed" } / chunk.rel_path()), compressed);
                chunk.compressed_size = compressed.size();
                chunk.compression_level = zstd::default_compression_level;
                chunk.last_block = { chunk.blocks.back().slot, chunk.blocks.back().hash };
                node.key() = chunk.end_offset() - 1;
                chunk_it = _chunks.insert(next_chunk_it, std::move(node));
                ++chunk_it;
            }
            while (chunk_it != _chunks.end()) {
                if (track_changes)
                    _truncated_chunks.emplace(chunk_it->first, chunk_it->second);
                chunk_it = _chunks.erase(chunk_it);
            }
            // reconfigure time if truncating back into Byron era
            if (_chunks.empty() || _chunks.rbegin()->second.blocks.back().era < 2)
                _cardano_cfg.shelley_start_epoch({});
        }
    }

    void chunk_registry::_my_start_tx()
    {
        _tx_progress_max.clear();
        _tx_progress_parse.store(0, std::memory_order_relaxed);
        _notify_end_offset = num_bytes();
        _notify_next_epoch = _chunks.empty() ? 0 : make_slot(_chunks.rbegin()->second.first_slot).epoch();
        if (!_unmerged_chunks.empty()) {
            logger::warn("unmerged chunks weren't empty at the beginning of a tx - recovering from an error?");
            _unmerged_chunks.clear();
        }
    }

    uint64_t chunk_registry::_my_end_offset() const
    {
        return num_bytes();
    }

    void chunk_registry::_my_prepare_tx()
    {
        timer t { "chunk_registry::_prepare_tx" };
        if (!_unmerged_chunks.empty()) {
            logger::warn("{} unmerged chunks - ignoring them", _unmerged_chunks.size());
            if (!_chunks.empty())
                logger::trace("last merged chunk: {}", json::serialize(_chunks.rbegin()->second.to_json()));
            for (const auto &[last_byte_offset, uchunk]: _unmerged_chunks)
                logger::trace("unmerged chunk with last byte offset {}: {}", last_byte_offset, json::serialize(uchunk.to_json()));
            _unmerged_chunks.clear();
        }
        {
            mutex::unique_lock update_lk { _update_mutex };
            _notify_of_updates(update_lk, true);
        }
        // let the operations potentially scheduled in _on_epoch_merge calls to finish
        _sched.process(true);
    }

    void chunk_registry::_my_rollback_tx()
    {
        const auto restore_offset = _truncated_chunks.empty()
            ? _transaction->start_offset()
            : std::min(_transaction->start_offset(), _truncated_chunks.begin()->second.offset);
        // New files belong to staging; reused live inputs remain available.
        _chunks.erase(_chunks.lower_bound(restore_offset), _chunks.end());
        for (auto &&[last_offset, chunk]: _truncated_chunks) {
            const auto chunk_path = full_path(chunk.rel_path());
            _file_remover.unmark(chunk_path);
            const auto [it, created] = _chunks.try_emplace(chunk.offset + chunk.data_size - 1, std::move(chunk));
            if (!created) [[unlikely]]
                throw error(fmt::format("rollback failed: couldn't reinsert chunk {}", chunk_path));
        }
        _truncated_chunks.clear();
        _unmerged_chunks.clear();
    }

    void chunk_registry::_my_commit_tx()
    {
        for (const auto &[last_offset, chunk]: _truncated_chunks)
            _file_remover.mark(full_path(chunk.rel_path()));
        _truncated_chunks.clear();
        // Only the boundary chunk and its successors can have changed.
        const auto boundary = _transaction->start_offset();
        for (auto it = _chunks.lower_bound(boundary ? boundary - 1 : 0); it != _chunks.end(); ++it)
            _file_remover.unmark(full_path(it->second.rel_path()));
    }

    void chunk_registry::_require_better_candidate_chain(const bool allow_existing_prefix)
    {
        const auto new_tip = tip();
        if (!new_tip || new_tip->end_offset <= _transaction->start_offset()) [[unlikely]] {
            throw candidate_chain_rejected_t(fmt::format("candidate chain is not better: proposed tip: {} intersection: {}", new_tip, _transaction->start));
        }
        if (!_truncated_chunks.empty()) {
            // Revalidating an unchanged prefix is not selection of a competing chain.
            if (allow_existing_prefix) {
                const auto old_chunk = _truncated_chunks.lower_bound(new_tip->end_offset - 1);
                if (old_chunk != _truncated_chunks.end()) {
                    const auto &blocks = old_chunk->second.blocks;
                    const auto old_block = std::lower_bound(blocks.begin(), blocks.end(), new_tip->end_offset,
                        [](const auto &block, const auto end_offset) { return block.end_offset() < end_offset; });
                    if (old_block != blocks.end() && old_block->point() == *new_tip) {
                        return;
                    }
                }
            }
            // slot window for the chain density calculation
            auto window_last_slot = cardano::density_default_window;
            ptrdiff_t fork_prev_height = 0;

            auto first_it = const_iterator::cbegin(*this, _truncated_chunks);
            const auto tr_cend = const_iterator::cend(*this, _truncated_chunks);
            if (_transaction->start_offset() > 0) {
                window_last_slot += _transaction->start->slot;
                while (first_it != tr_cend && first_it->offset < _transaction->start_offset()) {
                    ++first_it;
                }
            }
            if (first_it != const_iterator::cend(*this, _truncated_chunks)) {
                auto last_it = first_it;
                while (last_it != tr_cend && last_it->slot <= window_last_slot) {
                    ++last_it;
                }
                fork_prev_height = const_iterator::block_distance(last_it, first_it).height();
            }

            // some candidate blocks may have not passed the delayed steps of the validation
            const auto new_first_it = find_by_offset(_transaction->start_offset());
            const auto last_valid_slot = std::min(window_last_slot, new_tip->slot);
            auto new_last_it = new_first_it;
            const auto new_cend = cend();
            while (new_last_it != new_cend && new_last_it->slot <= last_valid_slot) {
                ++new_last_it;
            }
            const auto fork_new_height = const_iterator::block_distance(new_last_it, new_first_it).height();

            if (fork_prev_height >= fork_new_height) [[unlikely]] {
                throw candidate_chain_rejected_t(fmt::format("candidate chain at byte {} is not better than the original: candidate block count {} vs {}",
                    _transaction->start_offset(), fork_new_height, fork_prev_height));
            }
        }
    }

    // can commit progress while still returning the error that stopped the attempt
    [[nodiscard]] std::exception_ptr chunk_registry::_accept_progress(const cardano::optional_point &start, const std::optional<progress_point> &target,
            const bool aim_progress, const std::function<void()> &action) {
        if (_transaction || _journal)
            throw error("an unfinished transaction requires reopening the registry");
        bool started = false;
        const auto act_err = logger::run_log_errors([&] {
            _start_tx(start, target);
            started = true;
            action();
        });
        // Initialization can fail midway through processor truncation. Its
        // rollback callbacks are not yet initialized; reload the committed state.
        if (!started) {
            if (_transaction)
                throw error("transaction initialization interrupted; reopen the registry");
            _journal.reset();
            return act_err;
        }
        std::exception_ptr commit_err = nullptr;
        if (!act_err || aim_progress) {
            commit_err = logger::run_log_errors([&]{
                if (!aim_progress) {
                    _sched.process(true);
                    if (!_unmerged_chunks.empty()) [[unlikely]] {
                        throw error("cannot commit an import with unmerged chunks");
                    }
                }
                _prepare_tx();
                if (_before_commit) {
                    _before_commit();
                }
                if (aim_progress || num_bytes() > _transaction->start_offset()) {
                    _require_better_candidate_chain(!aim_progress);
                }
                _commit_tx();
            });
            if (!commit_err)
                return act_err;
        }
        if (_journal && _journal->decided()) {
            // The durable decision is to complete, never to roll back. Keep all
            // transaction state and require a reopen before any further mutation.
            throw error("commit interrupted; reopen the registry to complete the pending transaction");
        }
        logger::debug("rollback triggers: action error: {} commit error: {}", !!act_err, !!commit_err);
        logger::run_log_errors([&] { _sched.process(true); });
        // Before the journal's decision, discard staging and restore in-memory
        // state. Rollback failure propagates and prevents another transaction.
        _rollback_tx();
        logger::run_log_errors([&] { _sched.process(true); });
        return act_err ? act_err : commit_err;
    }

    void chunk_registry::_start_tx(cardano::optional_point start, const std::optional<progress_point> &target)
    {
        if (_mode != mode::validate) [[unlikely]] {
            throw error("chain changes require opening the registry in validation mode");
        }
        timer t { "chunk_registry::start_tx", logger::level::debug };
        if (_transaction || _journal) [[unlikely]]
            throw error("nested transactions are not allowed!");
        if (target < start) [[unlikely]]
            throw error(fmt::format("the target slot {} cannot be smaller than the start chain {}", target, start));
        if (start) {
            // checks that the requested start point is known
            const auto &block = get_block_info(cardano::point2 { start->slot, start->hash });
            // ensure we use the internally verified data about the start point
            start->height = block.height;
            start->end_offset = block.end_offset();
        }
        _checkpoint_requested.store(false, std::memory_order_release);
        storage::commit_journal::recover(_data_dir, true);
        _journal = std::make_unique<storage::commit_journal>(_data_dir);
        _transaction = active_transaction { start, target };
        for (const auto &[name, index]: _indexer->indexers())
            index->work_dir(stage_path("index"));
        _transaction->restore_ledger = _validator && valid_end_offset() == num_bytes();
        for (const auto &snapshot: _validator->snapshots())
            _transaction_pins.emplace_back(_file_remover.pin(read_path(
                std::filesystem::path { "validate" } / fmt::format("ledger-{:013}.bin", snapshot.end_offset))));
        // must happen before a potential truncate below
        if (!_truncated_chunks.empty()) {
            logger::warn("truncated chunks weren't empty at the beginning of a tx - recovering from an error?");
            _truncated_chunks.clear();
        }
        _do_truncate(_transaction->start, true);
        _my_start_tx();
        for (const auto *p: _processors) {
            if (p->start_tx)
                p->start_tx();
        }
    }

    void chunk_registry::_prepare_tx()
    {
        timer t { "chunk_registry::prepare_tx", logger::level::debug };
        if (!_transaction) [[unlikely]]
            throw error("prepare_tx can be executed only inside of a transaction!");
        _my_prepare_tx();
        for (const auto *p: _processors) {
            if (p->prepare_tx)
                p->prepare_tx();
        }
        _do_truncate(tip(), false);
        for (const auto *p: _processors) {
            if (p->stage_tx)
                p->stage_tx();
        }
        const auto boundary = _transaction->start_offset();
        for (auto it = _chunks.lower_bound(boundary ? boundary - 1 : 0); it != _chunks.end(); ++it)
            stage_output(std::filesystem::path { "compressed" } / it->second.rel_path());
        _stage_state(*_journal);
        _journal->seal();
    }

    void chunk_registry::_rollback_tx()
    {
        if (!_transaction) [[unlikely]]
            throw error("rollback_tx can be executed only inside of a transaction!");
        _my_rollback_tx();
        for (const auto &[name, index]: _indexer->indexers())
            index->work_dir({});
        _journal.reset();
        for (const auto *p: _processors) {
            if (p->rollback_tx)
                p->rollback_tx();
        }
        _transaction_pins.clear();
        const bool restore_ledger = _transaction->restore_ledger;
        _transaction.reset();
        if (restore_ledger)
            _validator->recover();
    }

    void chunk_registry::_commit_tx()
    {
        timer t { "chunk_registry::commit_tx", logger::level::debug };
        if (!_transaction) [[unlikely]]
            throw error("commit_tx can be executed only inside of a transaction!");
        // The journal enforces preparation and retains a failed commit for replay.
        _journal->commit();
        _state_needs_save = false;
        _journal->cleanup();
        for (const auto &[name, index]: _indexer->indexers())
            index->work_dir({});
        _my_commit_tx();
        for (const auto *p: _processors) {
            if (p->commit_tx)
                p->commit_tx();
        }
        _transaction_pins.clear();
        const auto target = _transaction->target;
        const bool completed = target && target->final_checkpoint
            && (target->end_offset ? num_bytes() >= target->end_offset : max_slot() >= target->slot);
        _transaction.reset();
        _journal.reset();
        const auto failure = logger::run_log_errors([&] { checkpoint(completed); });
        if (failure && _journal && _journal->decided())
            std::rethrow_exception(failure);
    }

    void chunk_registry::_stage_state(storage::commit_journal &journal) const
    {
        _file_remover.unmark(_state_path);
        save_chunk_registry_state(journal.stage("compressed/state.bin").string(), _chunks);
        journal.add("compressed/state.bin");
    }

    void chunk_registry::_commit_state(std::unique_ptr<storage::commit_journal> journal)
    {
        _stage_state(*journal);
        journal->seal();
        _journal = std::move(journal);
        scope_exit abort_preparation { [&] {
            if (_journal && !_journal->decided())
                _journal.reset();
        } };
        _journal->commit();
        _state_needs_save = false;
        _journal.reset();
    }

    void chunk_registry::_do_truncate(const cardano::optional_point &new_tip, const bool track_changes)
    {
        if (!_transaction) [[unlikely]]
            throw error("truncate can be executed only inside of a transaction!");
        if (new_tip < _transaction->start) [[unlikely]]
            throw error("truncation must happen only within the target transaction slot range!");
        logger::debug("truncate the local chain to {}", new_tip);
        _my_truncate(new_tip, track_changes);
        for (const auto *p: _processors) {
            if (p->truncate)
                p->truncate(new_tip, track_changes);
        }
    }

    void chunk_registry::_notify_of_updates(mutex::unique_lock &update_lk, bool force)
    {
        if (!update_lk) [[unlikely]]
            throw error("update_mutex must be locked when _notify_of_updates is called!");
        const auto max_epoch = make_slot(max_slot()).epoch();
        const auto end_offset = num_bytes();
        if (!force && _transaction->target && _transaction->target->slot == max_slot())
            force = true;
        while (end_offset > _notify_end_offset && (_notify_next_epoch < max_epoch || (force && _notify_next_epoch == max_epoch))) {
            // in unit-tests chunks may have non-continuous epochs
            if (_has_epoch(_notify_next_epoch, update_lk)) {
                const auto einfo = _epoch(_notify_next_epoch);
                epoch_info::chunk_list filtered_chunks {};
                for (const auto *chunk: einfo.chunks()) {
                    if (chunk->offset >= _notify_end_offset)
                        filtered_chunks.emplace_back(chunk);
                }
                _notify_end_offset = einfo.end_offset();
                if (!filtered_chunks.empty()) {
                    const epoch_info update_info { std::move(filtered_chunks) };
                    for (const auto *p: _processors) {
                        if (p->on_epoch_update)
                            p->on_epoch_update(_notify_next_epoch, update_info);
                    }
                }
            }
            ++_notify_next_epoch;
        }
    }

    chunk_registry::chunk_map::iterator chunk_registry::_find_chunk_by_offset(const uint64_t offset)
    {
        const auto it = _chunks.lower_bound(offset);
        if (it == _chunks.end()) [[unlikely]]
            throw error(fmt::format("no chunk matches offset: {}!", offset));
        return it;
    }

    chunk_registry::chunk_map::const_iterator chunk_registry::_find_chunk_by_offset_no_throw(const uint64_t offset) const
    {
        return _chunks.lower_bound(offset);
    }

    chunk_registry::chunk_map::const_iterator chunk_registry::_find_chunk_by_offset(const uint64_t offset) const
    {
        const auto it = _find_chunk_by_offset_no_throw(offset);
        if (it == _chunks.end()) [[unlikely]]
            throw error(fmt::format("no chunk matches offset: {}!", offset));
        return it;
    }

    storage::block_list::const_iterator chunk_registry::_find_block_by_offset(const chunk_map::const_iterator chunk_it, const uint64_t offset) const
    {
        if (chunk_it == _chunks.end()) [[unlikely]]
            throw error(fmt::format("internal error: a non-empty chunk_iterator is expected!"));
        const auto &blocks = chunk_it->second.blocks;
        const auto block_it = std::lower_bound(blocks.begin(), blocks.end(), offset,
            [](const auto &b, const auto offset) { return b.end_offset() - 1 < offset; });
        return block_it;
    }

    // can return the closest succeeding chunk if no chunk includes the block
    chunk_registry::chunk_map::const_iterator chunk_registry::_find_chunk_by_slot(const uint64_t slot) const
    {
        const auto chunk_it = std::lower_bound(_chunks.begin(), _chunks.end(), slot,
            [](const auto &c, const auto &slot) { return c.second.last_block.slot < slot; });
        return chunk_it;
    }

    // can return the closest succeeding block if there is no block at that slot
    storage::block_list::const_iterator chunk_registry::_find_block_by_slot(const chunk_map::const_iterator chunk_it, const uint64_t slot) const
    {
        if (chunk_it == _chunks.end()) [[unlikely]]
            throw error("internal error: a non-empty chunk_iterator is expected!");
        return std::lower_bound(chunk_it->second.blocks.begin(), chunk_it->second.blocks.end(), slot,
            [](const auto &b, const auto &slot) { return b.slot < slot; });
    }

    bool chunk_registry::_has_epoch(const uint64_t epoch, mutex::unique_lock &update_lk) const
    {
        if (!update_lk) [[unlikely]]
            throw error("internal error: update lock must be held at the call to _has_epoch");
        const auto chunk_it = std::lower_bound(_chunks.begin(), _chunks.end(), epoch,
            [this](const auto &el, const auto &epoch) { return make_slot(el.second.first_slot).epoch() < epoch; });
        return chunk_it != _chunks.end() && make_slot(chunk_it->second.first_slot).epoch() == epoch;
    }
}
