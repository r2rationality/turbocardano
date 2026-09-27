/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <atomic>
#include <deque>
#include <future>
#include <turbo/cardano/ledger/rules/fees/environment.hpp>
#include <turbo/cardano/ledger/rules/deposits/change.hpp>
#include <turbo/cardano/ledger/rules/ledger/transaction.hpp>
#include <turbo/cardano/ledger/rules/utxow/requirements.hpp>
#include <turbo/cardano/ledger/rules/utxo/amount.hpp>
#include <turbo/cardano/ledger/rules/utxo/collateral.hpp>
#include <turbo/cardano/ledger/rules/utxos/phase2.hpp>
#include <turbo/cardano/ledger/rules/utxow/conway-validation.hpp>
#include <turbo/cardano/common/cert.hpp>
#include <turbo/cardano/common/common.hpp>
#include <turbo/cardano/common/native-script.hpp>
#include <turbo/cardano/babbage/block.hpp>
#include <turbo/cardano/ledger/state.hpp>
#include <turbo/cardano/ledger/rules/utxow/reference-native.hpp>
#include <turbo/cbor/zero2.hpp>
#include <turbo/index/block-fees.hpp>
#include <turbo/index/timed-update.hpp>
#include <turbo/math/big-int.hpp>
#include <turbo/common/memory.hpp>
#include <turbo/common/scope-exit.hpp>
#include <turbo/plutus/context.hpp>
#include <turbo/plutus/costs-config.hpp>
#include <turbo/txwit/validator.hpp>

namespace turbo::txwit {
    using namespace cardano;
    using namespace cardano::ledger;
    using namespace plutus;

    using diagnostic_clock = std::chrono::steady_clock;
    static constexpr int tx_frame_zstd_level = 1;

    struct zstd_io_diagnostics_t {
        size_t compressed_bytes = 0;
        size_t serialized_bytes = 0;
        uint64_t decompress_ns = 0;
        uint64_t deserialize_ns = 0;
        uint64_t serialize_ns = 0;
        uint64_t compress_ns = 0;
    };

    static uint64_t _diagnostic_elapsed_ns(const diagnostic_clock::time_point start)
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(diagnostic_clock::now() - start).count());
    }

    static double _diagnostic_ms(const uint64_t ns)
    {
        return static_cast<double>(ns) / 1'000'000;
    }

    static double _diagnostic_mib(const size_t bytes)
    {
        return static_cast<double>(bytes) / (1U << 20);
    }

    template<typename T>
    static T _diagnostic_decode_zstd(const uint8_vector &compressed, zstd_io_diagnostics_t &diag)
    {
        uint8_vector serialized {};
        diag.compressed_bytes = compressed.size();
        auto start = diagnostic_clock::now();
        zstd::decompress(serialized, compressed);
        diag.decompress_ns = _diagnostic_elapsed_ns(start);
        diag.serialized_bytes = serialized.size();

        start = diagnostic_clock::now();
        auto val = zpp::deserialize<T>(serialized);
        diag.deserialize_ns = _diagnostic_elapsed_ns(start);
        return val;
    }

    template<typename T>
    static uint8_vector _diagnostic_encode_zstd(const T &val, zstd_io_diagnostics_t &diag)
    {
        auto start = diagnostic_clock::now();
        auto serialized = zpp::serialize(val);
        diag.serialize_ns = _diagnostic_elapsed_ns(start);
        diag.serialized_bytes = serialized.size();

        start = diagnostic_clock::now();
        auto compressed = zstd::compress(serialized, tx_frame_zstd_level);
        diag.compress_ns = _diagnostic_elapsed_ns(start);
        diag.compressed_bytes = compressed.size();
        return compressed;
    }

    static void _diagnostic_update_max(std::atomic_size_t &dst, const size_t val)
    {
        auto prev = dst.load(std::memory_order_relaxed);
        while (prev < val && !dst.compare_exchange_weak(prev, val, std::memory_order_relaxed)) {
        }
    }

    struct pipeline_diagnostics_t {
        std::atomic_size_t active_stage1 = 0;
        std::atomic_size_t max_active_stage1 = 0;
    };

    static pipeline_diagnostics_t &_pipeline_diagnostics()
    {
        static pipeline_diagnostics_t diag {};
        return diag;
    }

    struct stage1_batch_diagnostics_t {
        size_t epoch = 0;
        size_t chunks = 0;
        size_t blocks = 0;
        size_t txs = 0;
        size_t plutus_txs = 0;
        size_t invalid_txs = 0;
        size_t timed_updates = 0;
        size_t utxo_updates = 0;
        size_t ref_script_uses = 0;
        size_t input_compressed_bytes = 0;
        size_t input_serialized_bytes = 0;
        size_t tx_frame_compressed_bytes = 0;
        size_t tx_frame_serialized_bytes = 0;
        uint64_t input_read_ns = 0;
        uint64_t input_decompress_ns = 0;
        uint64_t parse_ns = 0;
        uint64_t sort_ns = 0;
        uint64_t tx_serialize_ns = 0;
        uint64_t tx_compress_ns = 0;
        size_t rss_start_mb = 0;
        size_t rss_during_parse_max_mb = 0;
        size_t rss_after_parse_mb = 0;
        size_t rss_after_tx_frames_mb = 0;
        bool complete = false;
    };

    struct validation_partition_diagnostics_t {
        zstd_io_diagnostics_t io {};
        size_t txs = 0;
        size_t plutus_txs = 0;
        uint64_t prep_context_ns = 0;
        uint64_t invariants_ns = 0;
        uint64_t plutus_prepare_ns = 0;
        uint64_t plutus_evaluate_ns = 0;
        uint64_t total_ns = 0;
    };

    using byron_witness_t = std::variant<tx_wit_byron_vkey, tx_wit_byron_redeemer>;

    struct balances_t {
        cardano::ledger::rules::amount_sum in_coin {};
        cardano::ledger::rules::amount_sum out_coin {};
        cardano::ledger::rules::asset_sums in_assets {};
        cardano::ledger::rules::asset_sums out_assets {};

#include <turbo/cardano/ledger/rules/utxo/balance.ipp>
    };

    // A compact way to reference a transaction in the same batch
    // ZPP serialization does not support bit fields. Thus, the manual bit manipulations.
    struct tx_loc_t {
        tx_loc_t() =default;

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self._val);
        }

        tx_loc_t(const uint8_t part_idx, const size_t tx_idx)
        {
            if (tx_idx > 0xFFFFFF) [[unlikely]]
                throw error("too many transactions in a batch partition");
            _val = (uint32_t { part_idx } << 24) | static_cast<uint32_t>(tx_idx);
        }

        uint8_t part_idx() const
        {
            return _val >> 24;
        }

        size_t tx_idx() const
        {
            return _val & 0xFFFFFF;
        }

        bool operator<(const tx_loc_t &o) const noexcept
        {
            return _val < o._val;
        }

        bool operator==(const tx_loc_t &o) const noexcept
        {
            return _val == o._val;
        }
    private:
        uint32_t _val = 0;
    };
    static_assert(sizeof(tx_loc_t) == 4);
}

namespace fmt {
    template<>
        struct formatter<turbo::txwit::balances_t>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::txwit::balances_t &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(
                ctx.out(),
                "in_coin: {} out_coin: {} in_assets: {} out_assets: {}",
                v.in_coin, v.out_coin, v.in_assets, v.out_assets
            );
        }
    };

    template<>
        struct formatter<turbo::txwit::bootstrap_signer_t>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::txwit::bootstrap_signer_t &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "bootstrap {}", v.root_hash);
        }
    };

    template<>
        struct formatter<turbo::txwit::script_signer_t>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "script {} {}", v.tag, v.hash);
        }
    };

    template<>
        struct formatter<turbo::txwit::vkey_signer_t>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "vkey {}", v.hash);
        }
    };

    template<>
        struct formatter<turbo::txwit::required_signer_t>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return std::visit([&](const auto &vv) -> decltype(ctx.out()) {
                return fmt::format_to(ctx.out(), "{}", vv);
            }, v.val);
        }
    };
}

namespace turbo::txwit {
    witness_type witness_type_from_str(const std::string_view s)
    {
        if (s == "all")
            return witness_type::all;
        if (s == "vkey")
            return witness_type::vkey;
        if (s == "script")
            return witness_type::script;
        if (s == "none")
            return witness_type::none;
        throw error(fmt::format("unsupported value of the wits options: {}", s));
    }

    struct processor::impl {
        impl(const chunk_registry &cr, state &st, const error_handler_func &on_error)
            : _cr { cr }, _st { st }, _cfg { {}, {}, witness_type::all, on_error },
                _processor { cr, _cfg, st }, _completed_offset { st.end_offset() }
        {
        }

        void reset()
        {
            mutex::scoped_lock lk { _cache_mutex };
            _cache.clear();
            _cache_bytes = 0;
            _direct.reset();
            _processor.refresh(true);
            _completed_offset = _st.end_offset();
        }

        uint64_t end_offset() const { return _completed_offset; }

        void cache(const block_hash &hash, const buffer bytes)
        {
            if (bytes.size() > direct_limit)
                return;
            mutex::scoped_lock lk { _cache_mutex };
            if (_cache_bytes + bytes.size() <= cache_limit) {
                const auto [it, inserted] = _cache.try_emplace(hash, bytes);
                if (inserted)
                    _cache_bytes += it->second.size();
            }
        }

        void apply(storage::chunk_cptr_list chunks, const optional_point &from,
            const optional_point &to, const witness_type type)
        {
            _cfg.intersection = from;
            _cfg.to = to;
            _cfg.typ = type;
            _cfg.replay_offset = _st.end_offset();
            _processor.refresh();
            std::ranges::sort(chunks, {}, &storage::chunk_info::offset);
            std::vector<storage::chunk_cptr_list> batches;
            for (const auto *chunk: chunks) {
                if (chunk->end_offset() <= _cfg.replay_offset || (to && chunk->offset >= to->end_offset))
                    continue;
                if (batches.empty() || batches.back().size() == 2
                        || _cr.make_slot(batches.back().front()->first_slot).epoch() != _cr.make_slot(chunk->first_slot).epoch())
                    batches.emplace_back();
                batches.back().push_back(chunk);
            }
            if (batches.empty())
                return;
            const auto batch_bytes = [&](const size_t i) {
                size_t size = 0;
                for (const auto *chunk: batches[i])
                    size += chunk->data_size;
                return size;
            };
            if (batches.size() == 1 && batch_bytes(0) <= direct_limit) {
                if (!_direct)
                    _direct = std::make_unique<batch_info>();
                _direct->reset();
                _prepare(batches.front(), 0, true, false, *_direct);
                _apply(*_direct);
                return;
            }
            _direct.reset();
            progress_guard pg { "txwit" };
            struct pending_batch {
                std::future<std::unique_ptr<batch_info>> value;
                size_t bytes;
            };
            std::deque<pending_batch> pending;
            scope_exit drain { [&] {
                for (auto &entry: pending)
                    if (entry.value.valid())
                        entry.value.wait();
            }};
            size_t next = 0, pending_bytes = 0;
            const auto replenish = [&] {
                while (next < batches.size() && pending.size() < _cr.sched().num_workers()) {
                    const auto bytes = batch_bytes(next);
                    if (!pending.empty() && pending_bytes + bytes > preparation_limit)
                        break;
                    const auto bi = next++;
                    auto task = std::make_shared<std::packaged_task<std::unique_ptr<batch_info>()>>([&, bi] {
                        auto part = std::make_unique<batch_info>();
                        const auto finalize = bi + 1 == batches.size()
                            || _cr.make_slot(batches[bi + 1].front()->first_slot).epoch() != _cr.make_slot(batches[bi].front()->first_slot).epoch();
                        _prepare(batches[bi], bi, finalize, true, *part);
                        return part;
                    });
                    pending.push_back({ task->get_future(), bytes });
                    pending_bytes += bytes;
                    _cr.sched().submit("txwit-prepare", -static_cast<int64_t>(bi), [task] { (*task)(); });
                }
            };
            replenish();
            while (!pending.empty()) {
                auto part = pending.front().value.get();
                _apply(*part);
                pending_bytes -= pending.front().bytes;
                pending.pop_front();
                part.reset();
                progress::get().update_inform("txwit", next - pending.size(), batches.size());
                replenish();
            }
        }
    private:
        // Batch maxima checked against the enacted protocol parameters.
        struct max_stats_t {
            std::optional<uint32_t> max_block_body_size {};
            std::optional<uint16_t> max_block_header_size {};
            std::optional<uint32_t> max_tx_size {};
        };

        struct batch_stats_t {
            size_t num_simple_txs = 0;
            size_t num_plutus_txs = 0;
            size_t num_invalid_txs = 0;
            wit_cnt wit_cnts {};

            batch_stats_t &operator+=(const batch_stats_t &o)
            {
                num_simple_txs += o.num_simple_txs;
                num_plutus_txs += o.num_plutus_txs;
                num_invalid_txs += o.num_invalid_txs;
                wit_cnts += o.wit_cnts;
                return *this;
            }
        };

        struct timed_update_info_t {
            index::timed_update::item update {};
            tx_loc_t tx_loc {};
            bool has_tx_loc = false;
            bool apply = true;

            bool operator<(const timed_update_info_t &o) const
            {
                return update < o.update;
            }
        };

        struct tx_context_t {
            tx_hash tx_id {};
            tx_loc_t tx_loc {};
            uint32_t ref_info_idx = 0; // chain-order reference join index, not tx_loc
            uint32_t tx_size = 0;
            uint64_t fee = 0;
            uint32_t slot = 0;
            uint8_t era = 0;
            bool reqires_genesis_delegs_quorum = false;
            bool phase2_valid = true;
            native_script::validity_interval interval {};
            cardano::ledger::rules::conway_validation conway {};
            std::optional<tx_output> collateral_return {};
            std::optional<uint64_t> total_collateral {};
            uint64_t collateral_fee = 0; // resolved stage-2 scratch
            balances_t balances {};
            stored_txo_list inputs {};
            stored_txo_list ref_inputs {};
            stored_txo_list collateral_inputs {};
            flat_set<script_hash> script_witnesses {};
            flat_set<script_hash> reference_scripts {}; // populated during UTxO resolution
            flat_set<required_signer_t> signers {};
            flat_set<required_signer_t> required_signers {};
            flat_set<script_hash> native_scripts {};
            flat_map<script_hash, std::optional<script_info>> native_script_refs {}; // filled in stage2 so does not need to be serialized
            std::vector<byron_witness_t> byron_signers {};
            std::optional<stored_tx_context> plutus_ctx {};

            static constexpr auto serialize(auto &archive, auto &self)
            {
                return archive(self.tx_id, self.tx_loc, self.ref_info_idx, self.tx_size, self.fee, self.slot, self.era, self.reqires_genesis_delegs_quorum,
                    self.phase2_valid, self.interval, self.conway, self.collateral_return, self.total_collateral,
                    self.balances, self.inputs, self.ref_inputs, self.collateral_inputs, self.script_witnesses,
                    self.signers, self.required_signers,
                    self.native_scripts, self.byron_signers, self.plutus_ctx);
            }

            static tx_context_t from_tx(const tx_loc_t &tx_loc, const tx_base &tx)
            {
                return {
                    .tx_id=tx.hash(),
                    .tx_loc=tx_loc,
                    .tx_size=numeric_cast<uint32_t>(tx.size()),
                    .fee=tx.block().era() > 1 ? tx.fee() : 0,
                    .slot=numeric_cast<uint32_t>(tx.block().slot()),
                    .era=numeric_cast<uint8_t>(tx.block().era()),
                    .phase2_valid=!tx.invalid(),
                    .interval={tx.validity_start(), tx.validity_end()}
                };
            }
        };

        using deposit_info_t = cardano::ledger::rules::deposit_change;

        struct ref_script_position_t {
            // Block height is the primary chain-order key; the transaction index
            // establishes order within a block without relying on slot uniqueness.
            uint32_t block_height = 0;
            uint32_t tx_idx = 0;

            bool operator==(const ref_script_position_t &) const =default;

            bool operator<(const ref_script_position_t &o) const
            {
                if (block_height != o.block_height)
                    return block_height < o.block_height;
                return tx_idx < o.tx_idx;
            }
        };

        struct ref_script_tx_info_t {
            tx_hash tx_id {};
            ref_script_position_t pos {};
            std::optional<tx_out_ref> regular_ref_overlap {};
        };

        struct ref_script_block_info_t {
            uint64_t slot = 0;
            uint32_t height = 0;
            uint32_t first_tx_idx = 0;
            uint32_t num_txs = 0;
        };

        struct ref_script_use_t {
            tx_out_ref id {};
            uint32_t tx_info_idx = 0;
            bool count_script = true;
        };

        struct ref_script_production_t {
            tx_out_ref id {};
            ref_script_position_t pos {};
            uint32_t script_size = 0;

            bool operator<(const ref_script_production_t &o) const
            {
                if (id != o.id)
                    return id < o.id;
                return pos < o.pos;
            }
        };

        struct ref_script_consumption_t {
            tx_out_ref id {};
            ref_script_position_t pos {};

            bool operator<(const ref_script_consumption_t &o) const
            {
                if (id != o.id)
                    return id < o.id;
                return pos < o.pos;
            }
        };

        struct ref_script_partition_t {
            std::vector<ref_script_use_t> uses {};
            std::vector<ref_script_production_t> produced {};
            std::vector<ref_script_consumption_t> consumed {};
        };

        struct batch_info {
            struct invalid_deposit_view {
                std::optional<tx_loc_t> tx {};
                std::map<stake_ident, cardano::ledger::rules::amount_sum> stake {}, drep {};
                std::map<pool_hash, bool> pools {};
            };
            static constexpr size_t num_parts = 256;

            size_t part_id = 0;
            size_t epoch = 0;
            bool finalize_after_batch = false;
            batch_stats_t stats {};
            // data to update the ledger state
            std::vector<index::block_fees::item> block_updates {};
            std::vector<timed_update_info_t> timed_updates {};
            txo_map utxos {};
            std::vector<ref_script_block_info_t> ref_script_blocks {};
            std::vector<ref_script_tx_info_t> ref_script_txs {};
            std::vector<ref_script_partition_t> ref_script_parts = std::vector<ref_script_partition_t>(num_parts);
            // pre-aggregated data for processing
            max_stats_t max_stats {};
            std::vector<std::vector<tx_context_t>> txs = std::vector<std::vector<tx_context_t>>(num_parts);
            // Independently compressed transaction partitions consumed in parallel by stage 2.
            std::vector<uint8_vector> tx_frames = std::vector<uint8_vector>(num_parts);
            // Stage-2 scratch data computed while applying the batch.
            invalid_deposit_view invalid_deposits {};
            std::map<tx_loc_t, deposit_info_t> tx_deposits {};
            std::vector<size_t> ref_script_sizes {};
            cardano::ledger::rules::amount_sum collateral_fees {};
            std::vector<uint8_vector> input_bytes {};
            std::vector<std::unique_ptr<block_container>> input_blocks {};
            std::vector<std::pair<const block_container *, const tx_base *>> signature_checks {};

            void reset()
            {
                stats = {};
                block_updates.clear();
                timed_updates.clear();
                for (size_t pi = 0; pi < num_parts; ++pi) {
                    utxos.partition(pi).clear();
                    ref_script_parts[pi].uses.clear();
                    ref_script_parts[pi].produced.clear();
                    ref_script_parts[pi].consumed.clear();
                    txs[pi].clear();
                    tx_frames[pi].clear();
                }
                ref_script_blocks.clear();
                ref_script_txs.clear();
                max_stats = {};
                invalid_deposits = {};
                tx_deposits.clear();
                ref_script_sizes.clear();
                collateral_fees = {};
                signature_checks.clear();
                input_blocks.clear();
                input_bytes.clear();
            }
        };

        struct validation_config_t {
            optional_point intersection;
            optional_point to;
            witness_type typ;
            const error_handler_func error_handler;
            uint64_t replay_offset = 0;

            template<typename T>
            static timed_update_info_t make_timed_update(const cert_loc_t &loc, T &&update,
                const tx_loc_t tx_loc={}, const bool has_tx_loc=false, const bool apply=true)
            {
                timed_update_info_t info {};
                info.update.loc = loc;
                info.update.update = std::forward<T>(update);
                info.tx_loc = tx_loc;
                info.has_tx_loc = has_tx_loc;
                info.apply = apply;
                return info;
            }

#include <turbo/cardano/ledger/rules/ledger/preprocess.ipp>

#include <turbo/cardano/ledger/rules/utxow/stage1.ipp>

#include <turbo/cardano/ledger/rules/utxos/stage2.ipp>
        };

        struct stage2_processor {
            stage2_processor(const chunk_registry &cr, const validation_config_t &cfg, state &st)
                : _cr { cr }, _cfg { cfg }, _st { st }
            {
                refresh();
            }

            void refresh(const bool force=false)
            {
                if (force || _environment_epoch != _st.epoch() || _environment_protocol != _st.params().protocol_ver) {
                    _refresh_protocol_environment();
                    _environment_epoch = _st.epoch();
                    _environment_protocol = _st.params().protocol_ver;
                }
            }

#include <turbo/cardano/ledger/rules/ledger/batch.ipp>

            uint64_t end_offset() const { return _st.end_offset(); }

            const wit_cnt &counts() const
            {
                return _cnts;
            }

        private:
            const chunk_registry &_cr;
            const validation_config_t &_cfg;
            state &_st;
            plutus_cost_models _cost_models_raw = _st.params().plutus_cost_models;
            costs::runtime_models _cost_models = costs::ingest(_cost_models_raw);
            cardano::ledger::rules::language_view_cache _language_views = cardano::ledger::rules::make_language_views(_cost_models_raw);
            cardano::ledger::rules::fee_environment _fees { _st.params() };
            wit_cnt _cnts {};
            std::optional<uint64_t> _environment_epoch {};
            protocol_version _environment_protocol {};

            void _refresh_protocol_environment()
            {
                // Fee parameters can change without changing any cost model.
                _fees = cardano::ledger::rules::fee_environment { _st.params() };
                if (_st.params().plutus_cost_models != _cost_models_raw) {
                    _cost_models_raw = _st.params().plutus_cost_models;
                    _cost_models = costs::ingest(_cost_models_raw);
                    _language_views = cardano::ledger::rules::make_language_views(_cost_models_raw);
                }
            }

            void _apply_epoch_update(const batch_info &part)
            {
                if (part.epoch > _st.epoch()) {
                    timer t { fmt::format("txwit batch: {} epoch: {} apply_epoch_update", part.part_id, part.epoch), logger::level::debug };
                    if (part.epoch != _st.epoch() + 1) [[unlikely]]
                        throw error(fmt::format("unexpected epoch: {} after: {}", part.epoch, _st.epoch()));
                    _st.start_epoch(part.epoch);
                    refresh();
                }
            }

#include <turbo/cardano/ledger/rules/deposits/observe.ipp>

#include <turbo/cardano/ledger/rules/ledger/batch-effects.ipp>

#include <turbo/cardano/ledger/rules/utxo/reference-scripts.ipp>

#include <turbo/cardano/ledger/rules/utxow/context.ipp>

            void _validate_byron_tx_invariants(const batch_info &part, tx_context_t &tx) const
            {
                // In Byron the difference between the inputs and the outputs is the fee, so not check that
                size_t byron_input_idx = 0;
                for (const auto &[id, data]: tx.inputs) {
                    const auto b_addr = byron_addr::from_bytes(data.address_raw);
                    std::visit([&](const auto &w) {
                        using T = std::decay_t<decltype(w)>;
                        if constexpr (std::is_same_v<T, tx_wit_byron_vkey>) {
                            if (!b_addr.vkey_ok(w.vkey, 0)) [[unlikely]]
                                throw error(fmt::format("epoch: {} slot: {} tx {} the byron witness #{} does not match the address: {}!",
                                    part.epoch, tx.slot, tx.tx_id, byron_input_idx, b_addr));
                        } else if constexpr (std::is_same_v<T, tx_wit_byron_redeemer>) {
                            if (!b_addr.vkey_ok(w.vkey, 2)) [[unlikely]]
                                throw error(fmt::format("epoch: {} slot: {} tx {} the byron witness #{} does not match the address: {}!",
                                    part.epoch, tx.slot, tx.tx_id, byron_input_idx, b_addr));
                        } else {
                            throw error(fmt::format("unsupported byron signer type: {}", typeid(T).name()));
                        }
                    }, tx.byron_signers.at(byron_input_idx));
                    ++byron_input_idx;
                }
            }

#include <turbo/cardano/ledger/rules/utxow/txwit.ipp>

#include <turbo/cardano/ledger/rules/utxo/txwit.ipp>

            void _validate_signatures(batch_info &part) const
            {
                if (part.signature_checks.empty())
                    return;
                const std::string task_id { "validate-signatures" };
                const auto workers = _cr.sched().num_workers();
                const auto step = (part.signature_checks.size() + workers - 1) / workers;
                std::vector<wit_cnt> counts((part.signature_checks.size() + step - 1) / step);
                _cr.sched().wait_all(task_id, [&](const auto &, const auto &submit) {
                    for (size_t start = 0; start < part.signature_checks.size(); start += step)
                        submit({ 2000, task_id, [&, start] {
                            for (size_t i = start; i < std::min(start + step, part.signature_checks.size()); ++i) {
                                const auto &[block, tx] = part.signature_checks[i];
                                counts[start / step] += _cfg.witnesses_ok_stage1(*block, *tx);
                            }
                        }});
                });
                for (const auto &count: counts)
                    part.stats.wit_cnts += count;
                part.signature_checks.clear();
            }

            wit_cnt _validate_witnesses(batch_info &part) const
            {
                timer t { fmt::format("txwit batch: {} epoch: {} par validate_witnesses", part.part_id, part.epoch), logger::level::debug };
                static const std::string task_id { "validate-batch" };
                auto &sched = _cr.sched();
                mutex::unique_lock::mutex_type part_mutex alignas(mutex::alignment) {};
                wit_cnt cnts {};
                const auto rss_before_mb = memory::my_usage_mb();
                if (part.tx_frames.size() != batch_info::num_parts) [[unlikely]]
                    throw error(fmt::format(
                        "invalid number of transaction frames: {} (expected: {})",
                        part.tx_frames.size(), batch_info::num_parts));
                std::array<validation_partition_diagnostics_t, batch_info::num_parts> diagnostics {};
                sched.wait_all(task_id, [&](const auto &, const auto &submit_f) {
                    for (size_t pi = 0; pi < batch_info::num_parts; ++pi) {
                        if (part.tx_frames[pi].empty() && part.txs[pi].empty())
                            continue;
                        submit_f({ 2000, task_id, [&, pi] {
                            const auto task_start = diagnostic_clock::now();
                            auto &diag = diagnostics[pi];
                            wit_cnt batch_cnts {};
                            cardano::ledger::rules::amount_sum collateral_fees {};
                            auto decoded = part.tx_frames[pi].empty() ? std::vector<tx_context_t> {}
                                : _diagnostic_decode_zstd<std::vector<tx_context_t>>(part.tx_frames[pi], diag.io);
                            auto &txs = part.tx_frames[pi].empty() ? part.txs[pi] : decoded;
                            diag.txs = txs.size();
                            for (auto &tx_ctx: txs) {
                                auto stage_start = diagnostic_clock::now();
                                auto plutus_ctx = _prep_plutus_ctx(tx_ctx);
                                diag.prep_context_ns += _diagnostic_elapsed_ns(stage_start);

                                stage_start = diagnostic_clock::now();
                                _validate_tx_invariants(part, tx_ctx, plutus_ctx.get());
                                diag.invariants_ns += _diagnostic_elapsed_ns(stage_start);
                                if (plutus_ctx) {
                                    ++diag.plutus_txs;
                                    if (tx_ctx.era == 7)
                                        plutus_ctx->set_script_purposes(std::move(tx_ctx.conway.credentials.purposes));
                                    plutus_ctx->cost_models(_cost_models);
                                    stage_start = diagnostic_clock::now();
                                    plutus_ctx->prepare();
                                    diag.plutus_prepare_ns += _diagnostic_elapsed_ns(stage_start);
                                    const auto &tx = plutus_ctx->tx();
                                    stage_start = diagnostic_clock::now();
                                    batch_cnts += _cfg.witnesses_ok_stage2(tx.block(), tx, *plutus_ctx, tx_ctx.phase2_valid);
                                    diag.plutus_evaluate_ns += _diagnostic_elapsed_ns(stage_start);
                                }
                                if (!tx_ctx.phase2_valid)
                                    collateral_fees += tx_ctx.collateral_fee;
                            }
                            txs.clear();
                            mutex::scoped_lock lk { part_mutex };
                            cnts += batch_cnts;
                            part.collateral_fees += collateral_fees;
                            diag.total_ns = _diagnostic_elapsed_ns(task_start);
                        }});
                    }
                });

                validation_partition_diagnostics_t total_diag {};
                size_t nonempty_parts = 0;
                size_t max_part_idx = 0;
                for (size_t pi = 0; pi < diagnostics.size(); ++pi) {
                    const auto &diag = diagnostics[pi];
                    if (diag.txs)
                        ++nonempty_parts;
                    total_diag.io.compressed_bytes += diag.io.compressed_bytes;
                    total_diag.io.serialized_bytes += diag.io.serialized_bytes;
                    total_diag.io.decompress_ns += diag.io.decompress_ns;
                    total_diag.io.deserialize_ns += diag.io.deserialize_ns;
                    total_diag.txs += diag.txs;
                    total_diag.plutus_txs += diag.plutus_txs;
                    total_diag.prep_context_ns += diag.prep_context_ns;
                    total_diag.invariants_ns += diag.invariants_ns;
                    total_diag.plutus_prepare_ns += diag.plutus_prepare_ns;
                    total_diag.plutus_evaluate_ns += diag.plutus_evaluate_ns;
                    total_diag.total_ns += diag.total_ns;
                    if (diag.total_ns > diagnostics[max_part_idx].total_ns)
                        max_part_idx = pi;
                }
                const auto &max_diag = diagnostics[max_part_idx];
                logger::debug(
                    "txwit validation diagnostics batch: {} epoch: {} partitions: {} nonempty_parts: {} txs: {} plutus_txs: {} "
                    "compressed_mib: {:.2f} serialized_mib: {:.2f} sum_decompress_ms: {:.3f} "
                    "sum_deserialize_ms: {:.3f} sum_prep_context_ms: {:.3f} sum_invariants_ms: {:.3f} "
                    "sum_plutus_prepare_ms: {:.3f} sum_plutus_evaluate_ms: {:.3f} "
                    "sum_partition_ms: {:.3f} max_partition: {} max_partition_txs: {} max_partition_plutus_txs: {} "
                    "max_partition_serialized_mib: {:.2f} max_partition_ms: {:.3f} rss_before_mb: {} rss_after_mb: {} peak_rss_mb: {}",
                    part.part_id, part.epoch, diagnostics.size(), nonempty_parts, total_diag.txs, total_diag.plutus_txs,
                    _diagnostic_mib(total_diag.io.compressed_bytes), _diagnostic_mib(total_diag.io.serialized_bytes),
                    _diagnostic_ms(total_diag.io.decompress_ns),
                    _diagnostic_ms(total_diag.io.deserialize_ns), _diagnostic_ms(total_diag.prep_context_ns),
                    _diagnostic_ms(total_diag.invariants_ns), _diagnostic_ms(total_diag.plutus_prepare_ns),
                    _diagnostic_ms(total_diag.plutus_evaluate_ns),
                    _diagnostic_ms(total_diag.total_ns), max_part_idx, max_diag.txs, max_diag.plutus_txs,
                    _diagnostic_mib(max_diag.io.serialized_bytes), _diagnostic_ms(max_diag.total_ns),
                    rss_before_mb, memory::my_usage_mb(), memory::max_usage_mb());
                return cnts;
            }

#include <turbo/cardano/ledger/rules/block-body/limits.ipp>

#include <turbo/cardano/ledger/rules/ledger/validate.ipp>
        };

        static constexpr size_t direct_limit = 1U << 20;
        static constexpr size_t cache_limit = 4U << 20;
        static constexpr size_t preparation_limit = 256U << 20;
        const chunk_registry &_cr;
        state &_st;
        validation_config_t _cfg;
        stage2_processor _processor;
        uint64_t _completed_offset = 0;
        std::unique_ptr<batch_info> _direct {};
        mutex::unique_lock::mutex_type _cache_mutex {};
        std::map<block_hash, uint8_vector> _cache {};
        size_t _cache_bytes = 0;

        void _apply(batch_info &part)
        {
            _processor.apply_batch(std::move(part));
            _completed_offset = _st.end_offset();
        }

        uint8_vector _read(const storage::chunk_info &chunk, stage1_batch_diagnostics_t &diag)
        {
            {
                mutex::scoped_lock lk { _cache_mutex };
                if (auto it = _cache.find(chunk.data_hash); it != _cache.end()) {
                    _cache_bytes -= it->second.size();
                    auto bytes = std::move(it->second);
                    _cache.erase(it);
                    return bytes;
                }
            }
            auto start = diagnostic_clock::now();
            const auto compressed = file::read(_cr.full_path(chunk.rel_path()));
            diag.input_read_ns += _diagnostic_elapsed_ns(start);
            diag.input_compressed_bytes += compressed.size();
            start = diagnostic_clock::now();
            auto bytes = zstd::decompress(compressed);
            diag.input_decompress_ns += _diagnostic_elapsed_ns(start);
            return bytes;
        }

        void _prepare(const storage::chunk_cptr_list &batch, const size_t batch_no,
            const bool finalize_after_batch, const bool compress, batch_info &part)
        {
            const auto &cr = _cr;
            const auto &cfg = _cfg;
            if (batch.empty()) [[unlikely]]
                throw error(fmt::format("batch {} is empty!", batch_no));
            stage1_batch_diagnostics_t diag {
                .epoch=cr.make_slot(batch.front()->first_slot).epoch(),
                .chunks=batch.size(),
                .rss_start_mb=memory::my_usage_mb(),
                .rss_during_parse_max_mb=memory::my_usage_mb()
            };
            auto &pipeline_diag = _pipeline_diagnostics();
            const auto active_stage1 = pipeline_diag.active_stage1.fetch_add(1, std::memory_order_relaxed) + 1;
            _diagnostic_update_max(pipeline_diag.max_active_stage1, active_stage1);
            scope_exit stage1_done { [&] {
                const auto active_after = pipeline_diag.active_stage1.fetch_sub(1, std::memory_order_relaxed) - 1;
                logger::debug(
                    "txwit stage1 timing diagnostics batch: {} epoch: {} complete: {} chunks: {} blocks: {} txs: {} plutus_txs: {} invalid_txs: {} "
                    "input_compressed_mib: {:.2f} input_serialized_mib: {:.2f} input_read_ms: {:.3f} input_decompress_ms: {:.3f} "
                    "parse_ms: {:.3f} sort_ms: {:.3f} tx_serialized_mib: {:.2f} tx_compressed_mib: {:.2f} "
                    "tx_serialize_ms: {:.3f} tx_compress_ms: {:.3f}",
                    batch_no, diag.epoch, diag.complete, diag.chunks, diag.blocks, diag.txs, diag.plutus_txs, diag.invalid_txs,
                    _diagnostic_mib(diag.input_compressed_bytes), _diagnostic_mib(diag.input_serialized_bytes),
                    _diagnostic_ms(diag.input_read_ns), _diagnostic_ms(diag.input_decompress_ns),
                    _diagnostic_ms(diag.parse_ns), _diagnostic_ms(diag.sort_ns),
                    _diagnostic_mib(diag.tx_frame_serialized_bytes), _diagnostic_mib(diag.tx_frame_compressed_bytes),
                    _diagnostic_ms(diag.tx_serialize_ns), _diagnostic_ms(diag.tx_compress_ns));
                logger::debug(
                    "txwit stage1 memory diagnostics batch: {} epoch: {} complete: {} timed_updates: {} utxo_updates: {} ref_script_uses: {} "
                    "rss_start_mb: {} rss_parse_max_mb: {} rss_after_parse_mb: {} rss_after_tx_frames_mb: {} "
                    "rss_after_handoff_mb: {} peak_rss_mb: {} active_stage1_before: {} "
                    "active_stage1_after: {} peak_active_stage1: {}",
                    batch_no, diag.epoch, diag.complete, diag.timed_updates, diag.utxo_updates, diag.ref_script_uses,
                    diag.rss_start_mb, diag.rss_during_parse_max_mb, diag.rss_after_parse_mb, diag.rss_after_tx_frames_mb,
                    memory::my_usage_mb(), memory::max_usage_mb(), active_stage1,
                    active_after, pipeline_diag.max_active_stage1.load(std::memory_order_relaxed));
            }};
            part.part_id = batch_no;
            part.epoch = cr.make_slot(batch.front()->first_slot).epoch();
            part.finalize_after_batch = finalize_after_batch;
            for (const auto *chunk_ptr: batch) {
                const auto &chunk = *chunk_ptr;
                const auto first_epoch = cr.make_slot(chunk.first_slot).epoch();
                const auto last_epoch = cr.make_slot(chunk.last_slot).epoch();
                if (first_epoch != part.epoch || last_epoch != part.epoch) [[unlikely]]
                    throw error(fmt::format("batch: {} contains data from multiple epochs: {}, {}, {}", batch_no, part.epoch, first_epoch, last_epoch));

                auto bytes = _read(chunk, diag);
                const auto &data = compress ? bytes : part.input_bytes.emplace_back(std::move(bytes));
                diag.input_serialized_bytes += data.size();

                const auto parse_start = diagnostic_clock::now();
                cbor::zero2::decoder dec { data };
                while (!dec.done()) {
                    auto &block_tuple = dec.read();
                    ++diag.blocks;
                    const auto offset = numeric_cast<uint64_t>(chunk.offset + block_tuple.data_begin() - data.data());
                    if (offset < cfg.replay_offset || (cfg.to && offset >= cfg.to->end_offset)) {
                        static_cast<void>(block_tuple.data_raw());
                        continue;
                    }
                    const auto prepare = [&](const block_container &blk) {
                        if (blk->era() > 0)
                            cfg.pre_aggregate_data(part, blk, !compress);
                        else
                            part.block_updates.push_back({ blk->slot(), blk->issuer_hash(), 0, 0, blk.end_offset(), 0 });
                    };
                    if (compress) {
                        const block_container blk { offset, block_tuple, cr.config() };
                        prepare(blk);
                    } else {
                        auto &blk = part.input_blocks.emplace_back(std::make_unique<block_container>(offset, block_tuple, cr.config()));
                        prepare(*blk);
                    }
                }
                diag.parse_ns += _diagnostic_elapsed_ns(parse_start);
                diag.rss_during_parse_max_mb = std::max(diag.rss_during_parse_max_mb, memory::my_usage_mb());
            }
            diag.rss_after_parse_mb = memory::my_usage_mb();
            const auto sort_start = diagnostic_clock::now();
            std::sort(part.block_updates.begin(), part.block_updates.end());
            std::sort(part.timed_updates.begin(), part.timed_updates.end());
            for (auto &ref_part: part.ref_script_parts) {
                std::sort(ref_part.produced.begin(), ref_part.produced.end());
                std::sort(ref_part.consumed.begin(), ref_part.consumed.end());
            }
            diag.sort_ns = _diagnostic_elapsed_ns(sort_start);
            diag.plutus_txs = part.stats.num_plutus_txs;
            diag.invalid_txs = part.stats.num_invalid_txs;
            diag.timed_updates = part.timed_updates.size();
            diag.utxo_updates = part.utxos.size();
            for (const auto &tx_part: part.txs)
                diag.txs += tx_part.size();
            for (const auto &ref_part: part.ref_script_parts)
                diag.ref_script_uses += ref_part.uses.size();
            if (compress) {
                for (size_t pi = 0; pi < batch_info::num_parts; ++pi) {
                    if (part.txs[pi].empty())
                        continue;
                    zstd_io_diagnostics_t tx_io_diag {};
                    part.tx_frames[pi] = _diagnostic_encode_zstd(part.txs[pi], tx_io_diag);
                    diag.tx_frame_compressed_bytes += tx_io_diag.compressed_bytes;
                    diag.tx_frame_serialized_bytes += tx_io_diag.serialized_bytes;
                    diag.tx_serialize_ns += tx_io_diag.serialize_ns;
                    diag.tx_compress_ns += tx_io_diag.compress_ns;
                    std::vector<tx_context_t> {}.swap(part.txs[pi]);
                }
            }
            diag.rss_after_tx_frames_mb = memory::my_usage_mb();
            diag.complete = true;
        }

        static void _del_utxo(batch_info &part, const txo_map::iterator it, bool created)
        {
            // If a txo is created and consumed within the same chunk, no need to report it further.
            if (!created) {
                if (it->second) [[likely]] {
                    part.utxos.erase(it);
                } else {
                    throw error(fmt::format("found a non-unique TXO in the same chunk {}", it->first));
                }
            }
        }

        static void _del_utxo(batch_info &part, const tx_out_ref &txo_id)
        {
            auto [it, created] = part.utxos.try_emplace(txo_id);
            _del_utxo(part, it, created);
        }

        static void _add_utxo(txo_map &idx, const tx_base &tx, const tx_output &txo, const size_t txo_idx)
        {
            if (const auto [it, created] = idx.try_emplace(tx_out_ref { tx.hash(), txo_idx }, txo); !created) [[unlikely]]
                throw error(fmt::format("found a non-unique TXO {}#{}", tx.hash(), txo_idx));
        }

    };

    processor::processor(const chunk_registry &cr, state &st, const error_handler_func &on_error)
        : _impl { std::make_unique<impl>(cr, st, on_error) }
    {
    }

    processor::~processor() =default;

    uint64_t processor::end_offset() const { return _impl->end_offset(); }

    void processor::reset() { _impl->reset(); }

    void processor::cache(const block_hash &hash, const buffer bytes) { _impl->cache(hash, bytes); }

    void processor::apply(storage::chunk_cptr_list chunks, const optional_point &from,
        const optional_point &to, const witness_type type)
    {
        _impl->apply(std::move(chunks), from, to, type);
    }

    optional_point validate(const chunk_registry &cr, const optional_point &intersection, const optional_point &to,
        const witness_type typ, const error_handler_func &error_handler)
    {
        state st { cr.config(), cr.sched() };
        st.start_epoch(0);
        if (intersection) {
            const auto path = cr.data_dir() / "validate" / "state.json";
            if (std::filesystem::exists(path)) {
                ::turbo::validator::snapshot_set snapshots;
                const auto metadata = json::load(path.string());
                for (const auto &entry: metadata.as_array()) {
                    auto snap = ::turbo::validator::snapshot::from_json(entry);
                    if (snap.format_version == ::turbo::validator::snapshot_format_version)
                        snapshots.insert(std::move(snap));
                }
                if (const auto *snap = snapshots.best([&](const auto &s) { return s.end_offset <= intersection->end_offset; })) {
                    st.clear(state::init_mode::empty);
                    st.load_zpp((cr.data_dir() / "validate" / fmt::format("ledger-{:013}.bin", snap->end_offset)).string());
                    if (st.end_offset() != snap->end_offset)
                        throw error("snapshot does not match its recorded offset");
                }
            }
        }
        storage::chunk_cptr_list chunks;
        if (st.end_offset() < cr.num_bytes())
            for (auto it = cr.find_offset_it(st.end_offset()); it != cr.chunks().end(); ++it) {
                if (to && it->second.offset >= to->end_offset)
                    break;
                chunks.push_back(&it->second);
            }
        processor proc { cr, st, error_handler };
        logger::run_log_errors([&] { proc.apply(std::move(chunks), intersection, to, typ); });
        logger::run_log_errors([&] { cr.sched().process(true); });
        return proc.end_offset() ? optional_point { cr.find_block_by_offset(proc.end_offset() - 1).point() } : optional_point {};
    }
}
