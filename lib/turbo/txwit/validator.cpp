/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <atomic>
#include <turbo/cardano/common/cert.hpp>
#include <turbo/cardano/common/common.hpp>
#include <turbo/cardano/common/native-script.hpp>
#include <turbo/cardano/babbage/block.hpp>
#include <turbo/cardano/ledger/state.hpp>
#include <turbo/cbor/zero2.hpp>
#include <turbo/index/block-fees.hpp>
#include <turbo/index/timed-update.hpp>
#include <turbo/math/big-int.hpp>
#include <turbo/common/memory.hpp>
#include <turbo/common/scope-exit.hpp>
#include <turbo/parallel/ordered-consumer.hpp>
#include <turbo/parallel/ordered-queue.hpp>
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
        std::atomic_size_t ready_batches = 0;
        std::atomic_size_t max_ready_batches = 0;
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
        size_t ready_batches = 0;
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

    struct byron_signer_t {
        uint8_vector addr {};

        bool operator<(const byron_signer_t& o) const
        {
            return addr < o.addr;
        }
    };

    using byron_witness_t = std::variant<tx_wit_byron_vkey, tx_wit_byron_redeemer>;

    struct vkey_signer_t {
        key_hash hash {};

        bool operator<(const vkey_signer_t& o) const
        {
            return hash < o.hash;
        }
    };

    struct script_signer_t {
        script_hash hash {};
        redeemer_tag tag = redeemer_tag::spend;

        bool operator<(const script_signer_t& o) const
        {
            if (tag != o.tag)
                return tag < o.tag;
            return hash < o.hash;
        }
    };

    struct bootstrap_signer_t {
        key_hash root_hash {};
        uint8_t typ = 0;

        bool operator<(const bootstrap_signer_t& o) const
        {
            if (typ != o.typ)
                return typ < o.typ;
            return root_hash < o.root_hash;
        }
    };

    struct required_signer_t {
        using value_type = std::variant<vkey_signer_t, script_signer_t, bootstrap_signer_t>;

        value_type val;

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.val);
        }

        static required_signer_t from_address(const redeemer_tag typ, const address &addr)
        {
            switch (const auto pay_id = addr.pay_id(); pay_id.type) {
                case pay_ident::ident_type::BYRON_KEY: {
                    const auto b_addr = addr.byron();
                    return { bootstrap_signer_t { b_addr.root(), b_addr.type() } };
                }
                case pay_ident::ident_type::SHELLEY_KEY:
                    return { vkey_signer_t { pay_id.hash } };
                case pay_ident::ident_type::SHELLEY_SCRIPT:
                    return { script_signer_t { pay_id.hash, typ } };
                default:
                    throw error(fmt::format("unsupported pay_ident type: {}", static_cast<int>(pay_id.type)));
            }
        }

        static required_signer_t from_cred(const credential_t &cred)
        {
            if (cred.script)
                return { script_signer_t { cred.hash, redeemer_tag::cert } };
            return { vkey_signer_t { cred.hash } };
        }

        // required only for zpp serialization methods
        required_signer_t() =default;

        required_signer_t(value_type &&v): val { std::move(v) }
        {
        }

        required_signer_t(const required_signer_t &v): val { v.val }
        {
        }

        required_signer_t(required_signer_t &&v): val { std::move(v.val) }
        {
        }

        required_signer_t &operator=(const required_signer_t &) =default;
        required_signer_t &operator=(required_signer_t &&) =default;

        required_signer_t(const redeemer_tag typ, const address &addr): required_signer_t { from_address(typ, addr) }
        {
        }

        required_signer_t(const credential_t cred): required_signer_t { from_cred(cred) }
        {
        }

        bool operator<(const required_signer_t &o) const
        {
            return std::visit<bool>([&](const auto &v, const auto &ov) {
                using T1 = std::decay_t<decltype(v)>;
                using T2 = std::decay_t<decltype(ov)>;
                if constexpr (!std::is_same_v<T1, T2>)
                    return val.index() < o.val.index();
                if constexpr (std::is_same_v<T1, T2>)
                    return v < ov;
            }, val, o.val);
        }
    };

    struct balances_t {
        uint64_t in_coin = 0;
        uint64_t out_coin = 0;
        multi_asset_map in_assets {};
        multi_asset_map out_assets {};

        bool match() const
        {
            return in_coin == out_coin && in_assets == out_assets;
        }
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
            _val = (part_idx << 24) | (tx_idx & 0xFFFFFF);
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
            using turbo::cardano::amount;
            return fmt::format_to(
                ctx.out(),
                "in_coin: {} out_coin: {} in_assets: {} out_assets: {}",
                amount { v.in_coin }, amount { v.out_coin }, v.in_assets, v.out_assets
            );
        }
    };

    template<>
        struct formatter<turbo::txwit::bootstrap_signer_t>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::txwit::bootstrap_signer_t &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "bootstrap {} {}", v.typ, v.root_hash);
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
        struct formatter<turbo::txwit::byron_witness_t>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{} {}", v.typ, v.vk);
        }
    };

    template<>
        struct formatter<turbo::txwit::byron_signer_t>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "byron {}", v.addr);
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

    struct validator {
        validator(const chunk_registry &cr, const optional_point &intersection, const optional_point &to,
                const witness_type typ, const error_handler_func &error_handler):
            _cr { cr }, _cfg { intersection, to, typ, error_handler }
        {
        }

        [[nodiscard]] optional_point validate() const
        {
            progress_guard pg { "txwit" };
            auto &pipeline_diag = _pipeline_diagnostics();
            pipeline_diag.active_stage1.store(0, std::memory_order_relaxed);
            pipeline_diag.max_active_stage1.store(0, std::memory_order_relaxed);
            pipeline_diag.ready_batches.store(0, std::memory_order_relaxed);
            pipeline_diag.max_ready_batches.store(0, std::memory_order_relaxed);

            const auto proc = std::make_shared<stage2_processor>(_cr, _cfg);
            const auto batches = proc->prepare_batches();
            const auto num_batches = batches.size();
            // ordered_queue publishes each populated slot before the ordered consumer is advanced.
            std::vector<std::unique_ptr<batch_info>> batch_handoff(num_batches);
            std::function<void(size_t)> replenish_stage1 {};

            parallel::ordered_consumer batch_consumer {
                [this, proc, num_batches, &batch_handoff, &replenish_stage1](const auto part_no) {
                    timer t { fmt::format("txwit batch: {} consume_batch", part_no), logger::level::debug };
                    auto &pipeline_diag = _pipeline_diagnostics();
                    const auto ready_at_start = pipeline_diag.ready_batches.load(std::memory_order_relaxed);
                    scope_exit consume_done { [&] {
                        pipeline_diag.ready_batches.fetch_sub(1, std::memory_order_relaxed);
                    }};
                    const auto rss_before_mb = memory::my_usage_mb();
                    auto part = std::move(batch_handoff.at(part_no));
                    if (!part) [[unlikely]]
                        throw error(fmt::format("txwit batch {} has no prepared stage-1 handoff", part_no));
                    const auto epoch = part->epoch;
                    try {
                        proc->apply_batch(std::move(*part));
                    } catch (...) {
                        if (!part->tx_frames.empty()) {
                            try {
                                _dump_tx_frames(_cr, *part);
                            } catch (const std::exception &ex) {
                                logger::error("failed to dump transaction frames for txwit batch {}: {}", part_no, ex.what());
                            }
                        }
                        throw;
                    }
                    logger::debug(
                        "txwit memory diagnostics batch: {} epoch: {} rss_before_mb: {} rss_after_apply_mb: {} "
                        "peak_rss_mb: {} ready_batches: {} active_stage1: {}",
                        part_no, epoch, rss_before_mb, memory::my_usage_mb(),
                        memory::max_usage_mb(), ready_at_start,
                        pipeline_diag.active_stage1.load(std::memory_order_relaxed));
                    progress::get().update_inform("txwit", part_no, num_batches);
                    if (replenish_stage1)
                        replenish_stage1(part_no + 1);
                },
                "consumer-part", 500, _cr.sched()
            };

            logger::run_log_errors([&, proc] {
                auto stats = _process_batches(batch_consumer, batches, batch_handoff, replenish_stage1);
                logger::debug("txwit: stage-1 txs: {} stage-2 txs: {} invalid txs: {}",
                    stats.num_simple_txs, stats.num_plutus_txs, stats.num_invalid_txs);
                logger::debug("txwit: stage-1: witnesses: {}", stats.wit_cnts);
                logger::debug("txwit: stage-2: witnesses: {}", proc->counts());
                stats.wit_cnts += proc->counts();
                logger::info("txwit: total: witnesses: {}", stats.wit_cnts);
            });
            const auto batch_end = batch_consumer.next();
            logger::debug("txwit: batch_end: {}", batch_end);
            if (batch_end > 0)
                return batches[batch_end - 1].back()->blocks.back().point();
            return _cfg.intersection;
        }
    private:
        // Examples of typical protocol-parameter-based checks
        struct max_stats_t {
            std::optional<uint32_t> max_block_body_size {};
            std::optional<uint16_t> max_block_header_size {};
            std::optional<uint32_t> max_tx_size {};

            static constexpr auto serialize(auto &archive, auto &self)
            {
                return archive(self.max_block_body_size, self.max_block_header_size, self.max_tx_size);
            }
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

            static constexpr auto serialize(auto &archive, auto &self)
            {
                return archive(self.update, self.tx_loc, self.has_tx_loc);
            }

            bool operator<(const timed_update_info_t &o) const
            {
                return update < o.update;
            }
        };

        struct tx_context_t {
            tx_hash tx_id {};
            tx_loc_t tx_loc {};
            uint32_t tx_size = 0;
            uint64_t fee = 0;
            uint32_t slot = 0;
            uint8_t era = 0;
            bool reqires_genesis_delegs_quorum = false;
            balances_t balances {};
            stored_txo_list inputs {};
            stored_txo_list ref_inputs {};
            flat_set<required_signer_t> signers {};
            flat_set<required_signer_t> required_signers {};
            flat_set<script_hash> native_scripts {};
            flat_map<script_hash, std::optional<script_info>> native_script_refs {}; // filled in stage2 so does not need to be serialized
            std::vector<byron_witness_t> byron_signers {};
            std::optional<stored_tx_context> plutus_ctx {};

            static constexpr auto serialize(auto &archive, auto &self)
            {
                return archive(self.tx_id, self.tx_loc, self.tx_size, self.fee, self.slot, self.era, self.reqires_genesis_delegs_quorum,
                    self.balances, self.inputs, self.ref_inputs,
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
                    .era=numeric_cast<uint8_t>(tx.block().era())
                };
            }
        };

        struct deposit_info_t {
            uint64_t in_coin = 0;
            uint64_t out_coin = 0;
        };

        struct ref_script_position_t {
            // Block height is the primary chain-order key; the transaction index
            // establishes order within a block without relying on slot uniqueness.
            uint32_t block_height = 0;
            uint32_t tx_idx = 0;

            static constexpr auto serialize(auto &archive, auto &self)
            {
                return archive(self.block_height, self.tx_idx);
            }

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
            bool phase2_valid = true;
            std::optional<tx_out_ref> regular_ref_overlap {};

            static constexpr auto serialize(auto &archive, auto &self)
            {
                return archive(self.tx_id, self.pos, self.phase2_valid, self.regular_ref_overlap);
            }
        };

        struct ref_script_block_info_t {
            uint64_t slot = 0;
            uint32_t height = 0;
            uint32_t first_tx_idx = 0;
            uint32_t num_txs = 0;

            static constexpr auto serialize(auto &archive, auto &self)
            {
                return archive(self.slot, self.height, self.first_tx_idx, self.num_txs);
            }
        };

        struct ref_script_use_t {
            tx_out_ref id {};
            uint32_t tx_info_idx = 0;

            static constexpr auto serialize(auto &archive, auto &self)
            {
                return archive(self.id, self.tx_info_idx);
            }
        };

        struct ref_script_production_t {
            tx_out_ref id {};
            ref_script_position_t pos {};
            uint32_t script_size = 0;

            static constexpr auto serialize(auto &archive, auto &self)
            {
                return archive(self.id, self.pos, self.script_size);
            }

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

            static constexpr auto serialize(auto &archive, auto &self)
            {
                return archive(self.id, self.pos);
            }

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

            static constexpr auto serialize(auto &archive, auto &self)
            {
                return archive(self.uses, self.produced, self.consumed);
            }
        };

        struct batch_info {
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
            // Transaction contexts are compressed into tx_frames and released
            // before this object is handed to the ordered consumer.
            std::vector<std::vector<tx_context_t>> txs = std::vector<std::vector<tx_context_t>>(num_parts);
            // Independently compressed transaction partitions consumed in parallel by stage 2.
            std::vector<uint8_vector> tx_frames = std::vector<uint8_vector>(num_parts);
            // Stage-2 scratch data computed while applying the batch.
            std::map<tx_loc_t, deposit_info_t> tx_deposits {};
            std::map<tx_hash, size_t> ref_script_sizes {};
        };

        struct validation_config_t {
            const optional_point intersection;
            const optional_point to;
            const witness_type typ;
            const error_handler_func error_handler;

            template<typename T>
            static timed_update_info_t make_timed_update(const cert_loc_t &loc, T &&update,
                const tx_loc_t tx_loc={}, const bool has_tx_loc=false)
            {
                timed_update_info_t info {};
                info.update.loc = loc;
                info.update.update = std::forward<T>(update);
                info.tx_loc = tx_loc;
                info.has_tx_loc = has_tx_loc;
                return info;
            }

            // Ledger updates must be always processed so witness checks see the correct state.
            // transaction data may be skipped
            void pre_aggregate_data(batch_info &part, const block_container &blk) const
            {
                auto &stats = part.stats;
                auto &max = part.max_stats;
                uint64_t fees = 0;
                uint64_t donations = 0;
                if (!max.max_block_body_size || *max.max_block_body_size < blk->body_size())
                    max.max_block_body_size = numeric_cast<uint32_t>(blk->body_size());
                if (!max.max_block_header_size || *max.max_block_header_size < blk->header().size())
                    max.max_block_header_size = numeric_cast<uint16_t>(blk->header().size());
                blk->foreach_update_proposal([&](const auto &prop) {
                    part.timed_updates.emplace_back(make_timed_update(cert_loc_t { blk->slot(), 0, 0 }, prop));
                });
                blk->foreach_update_vote([&](const auto &vote) {
                    part.timed_updates.emplace_back(make_timed_update(cert_loc_t { blk->slot(), 0, 0 }, vote));
                });
                // Reference-script size accounting was introduced in protocol version 9,
                // which starts with the Conway era. Keep only compact join metadata here;
                // copying complete Babbage-and-later TXOs made the ordered stage replay an
                // ever-growing UTXO overlay serially.
                if (blk->era() >= 7) {
                    auto &ref_block = part.ref_script_blocks.emplace_back();
                    ref_block.slot = blk->slot();
                    ref_block.height = numeric_cast<uint32_t>(blk->height());
                    ref_block.first_tx_idx = numeric_cast<uint32_t>(part.ref_script_txs.size());
                    for (const auto &tx_ptr: blk->txs()) {
                        const auto &tx = *tx_ptr;
                        const ref_script_position_t pos {
                            ref_block.height,
                            numeric_cast<uint32_t>(tx.index())
                        };
                        const auto tx_info_idx = numeric_cast<uint32_t>(part.ref_script_txs.size());
                        auto &ref_tx = part.ref_script_txs.emplace_back();
                        ref_tx = {
                            .tx_id=tx.hash(),
                            .pos=pos,
                            .phase2_valid=!tx.invalid()
                        };

                        flat_set<tx_out_ref> inputs {};
                        tx.foreach_input([&](const tx_input &input) {
                            inputs.emplace(input.hash, input.idx);
                        });
                        auto referenced = inputs;
                        tx.foreach_referenced_input([&](const tx_input &input) {
                            const tx_out_ref id { input.hash, input.idx };
                            if (inputs.contains(id))
                                ref_tx.regular_ref_overlap = id;
                            referenced.emplace(id);
                        });
                        for (const auto &id: referenced) {
                            auto &ref_part = part.ref_script_parts[txo_map::partition_idx(id)];
                            ref_part.uses.push_back({ id, tx_info_idx });
                        }

                        const auto add_consumption = [&](const tx_out_ref &id) {
                            auto &ref_part = part.ref_script_parts[txo_map::partition_idx(id)];
                            ref_part.consumed.push_back({ id, pos });
                        };
                        const auto add_production = [&](const tx_out_ref &id, const tx_output &output) {
                            auto &ref_part = part.ref_script_parts[txo_map::partition_idx(id)];
                            ref_part.produced.push_back({
                                id,
                                pos,
                                output.script_ref
                                    ? numeric_cast<uint32_t>(output.script_ref->script().size())
                                    : 0
                            });
                        };
                        if (tx.invalid()) {
                            tx.foreach_collateral([&](const tx_input &input) {
                                add_consumption(tx_out_ref { input.hash, input.idx });
                            });
                            if (const auto *babbage_tx = dynamic_cast<const cardano::babbage::tx_base *>(&tx); babbage_tx) {
                                if (const auto &collateral_return = babbage_tx->collateral_return(); collateral_return) {
                                    add_production(
                                        tx_out_ref { tx.hash(), tx_out_idx { tx.outputs().size() } },
                                        *collateral_return);
                                }
                            }
                        } else {
                            for (const auto &id: inputs)
                                add_consumption(id);
                            size_t output_idx = 0;
                            tx.foreach_output([&](const tx_output &output) {
                                add_production(
                                    tx_out_ref { tx.hash(), tx_out_idx { output_idx++ } },
                                    output);
                            });
                        }
                    }
                    ref_block.num_txs = numeric_cast<uint32_t>(part.ref_script_txs.size() - ref_block.first_tx_idx);
                }
                const auto block_info = storage::block_info::from_block(blk);
                blk->foreach_tx([&](const tx_base &tx) {
                    const uint8_t tx_part_idx = tx.hash()[0];
                    const size_t tx_idx = part.txs[tx_part_idx].size();
                    tx_loc_t tx_loc { tx_part_idx, tx_idx };
                    auto tx_checks = tx_context_t::from_tx(tx_loc, tx);
                    const auto witness_count = tx.witnesses().size();
                    tx_checks.signers.reserve(witness_count);
                    tx_checks.native_scripts.reserve(witness_count);
                    tx_checks.byron_signers.reserve(witness_count);
                    if (tx.block().era() > 1) {
                        fees += tx.fee();
                        tx_checks.balances.out_coin += tx.fee();
                    }
                    if (!max.max_tx_size || *max.max_tx_size < tx.raw().size())
                        max.max_tx_size = numeric_cast<uint32_t>(tx.raw().size());
                    const auto num_redeemers = tx.redeemers().size();
                    if (const auto start_slot = tx.validity_start(); start_slot) {
                        if (*start_slot > blk->slot()) [[unlikely]]
                            throw error(fmt::format("tx {} validity start interval: {} starts after the block's slot: {}",
                                tx.hash(), *start_slot, blk->slot()));
                    }
                    if (const auto end_slot = tx.validity_end(); end_slot) {
                        if (*end_slot <= blk->slot()) [[unlikely]] {
                            // when validity_start is not defined, the validity_end slot is inclusive!
                            if (*end_slot != blk->slot() || !tx.validity_end())
                                throw error(fmt::format("tx {} validity end interval: {} ends before the block's slot: {}",
                                    tx.hash(), *end_slot, blk->slot()));
                        }
                    }
                    if (num_redeemers) {
                        tx_checks.plutus_ctx.emplace(stored_tx_context {
                            .tx_id=tx.hash(),
                            .num_redeemers=num_redeemers,
                            .body=uint8_vector { tx.raw() },
                            .wits=uint8_vector { tx.witness_raw() },
                            // Stage 1 has no ledger state. Stage 2 fills the enacted protocol version.
                            .block=block_info
                        });
                        ++stats.num_plutus_txs;
                    } else {
                        ++stats.num_simple_txs;
                    }
                    tx.foreach_referenced_input([&](const tx_input &txi) {
                        auto &txo = tx_checks.ref_inputs.emplace_back(txi);
                        if (const auto txo_it = part.utxos.find(txo.id); txo_it != part.utxos.end())
                            txo.data = txo_it->second;
                    });
                    stats.wit_cnts += witnesses_ok_stage1(blk, tx);
                    size_t cert_idx = 0;
                    tx.foreach_cert([&](const auto &cert) {
                        if (std::holds_alternative<instant_reward_cert>(cert.val))
                            tx_checks.reqires_genesis_delegs_quorum = true;
                        const cert_loc_t loc { blk->slot(), tx.index(), cert_idx++ };
                        std::visit([&](const auto &c) {
                            part.timed_updates.emplace_back(make_timed_update(loc, c, tx_loc, true));
                        }, cert.val);
                        if (const auto r_cred = cert.signing_cred(); r_cred)
                            tx_checks.required_signers.emplace(*r_cred);
                    });
                    tx.foreach_script([&](const auto &s) {
                        if (s.type() == script_type::native)
                            tx_checks.native_scripts.emplace(s.hash());
                    });
                    // The available reward balance is checked by the consensus verification. No need to recheck it here.
                    tx.foreach_withdrawal([&](const tx_withdrawal &withdr) {
                        const auto stake_id = withdr.address.stake_id();
                        part.timed_updates.emplace_back(make_timed_update(
                            cert_loc_t { blk->slot(), tx.index(), 0 },
                            index::timed_update::stake_withdraw { stake_id, withdr.amount }
                        ));
                        if (withdr.amount) {
                            tx_checks.balances.in_coin += withdr.amount;
                            if (stake_id.script)
                                tx_checks.required_signers.emplace(script_signer_t { stake_id.hash, redeemer_tag::reward });
                            else
                                tx_checks.required_signers.emplace(vkey_signer_t { stake_id.hash });
                        }
                    });
                    size_t txo_idx = 0;
                    tx.foreach_output([&](const tx_output &txo) {
                        tx_checks.balances.out_coin += txo.coin;
                        if (const auto addr = txo.addr(); !addr.is_byron()) {
                            if (addr.network() != blk->config().shelley_network_id) [[unlikely]]
                                throw error(fmt::format("the network id of a shelley address: {} does not match the config: {}", addr, blk->config().shelley_network_id));
                        }
                        for (const auto &[policy_id, p_assets]: txo.assets) {
                            for (const auto &[name, coin]: p_assets) {
                                if (coin)
                                    tx_checks.balances.out_assets[policy_id][name] += coin;
                            }
                        }
                        _add_utxo(part.utxos, tx, txo, txo_idx++);
                    });
                    tx.foreach_required_signer([&](const auto vkey) {
                        tx_checks.required_signers.emplace(vkey_signer_t { vkey });
                    });
                    size_t num_inputs = 0;
                    tx.foreach_input([&](const tx_input &txin) {
                        auto &txo = tx_checks.inputs.emplace_back(txin);
                        const auto [it, created] = part.utxos.try_emplace(txo.id);
                        if (!created)
                            txo.data = it->second;
                        _del_utxo(part, it, created);
                        ++num_inputs;
                    });
                    if (!num_inputs) [[unlikely]]
                        throw error(fmt::format("tx {} does not have any inputs!", tx.hash()));
                    const auto donation = tx.donation();
                    donations += donation;
                    tx_checks.balances.out_coin += donation;
                    if (const auto *c_tx = dynamic_cast<const cardano::conway::tx *>(&tx); c_tx) {
                        index::timed_update::conway_tx_prelude prelude {
                            .has_proposals=!c_tx->proposals().empty()
                        };
                        for (const auto &v: c_tx->votes()) {
                            if (v.voter.type == voter_t::type_t::drep_key
                                    || v.voter.type == voter_t::type_t::drep_script) {
                                prelude.voting_dreps.emplace(
                                    v.voter.hash,
                                    v.voter.type == voter_t::type_t::drep_script);
                            }
                        }
                        tx.foreach_withdrawal([&](const tx_withdrawal &withdr) {
                            prelude.withdrawals.push_back({ reward_id_t { withdr.address.bytes() }, withdr.amount });
                        });
                        if (prelude.has_proposals || !prelude.voting_dreps.empty() || !prelude.withdrawals.empty()) {
                            part.timed_updates.emplace_back(make_timed_update(
                                cert_loc_t { blk->slot(), tx.index(), 0 },
                                std::move(prelude)
                            ));
                        }
                        size_t prop_idx = 0;
                        for (const auto &p: c_tx->proposals()) {
                            part.timed_updates.emplace_back(make_timed_update(
                                cert_loc_t { blk->slot(), tx.index(), prop_idx++ }, p
                            ));
                            tx_checks.balances.out_coin += p.procedure.deposit;
                        }
                        size_t vote_idx = 0;
                        for (const auto &v: c_tx->votes()) {
                            part.timed_updates.emplace_back(make_timed_update(
                                cert_loc_t { blk->slot(), tx.index(), vote_idx++ }, v
                            ));
                        }
                    }
                    tx.foreach_witness_byron_vkey([&](const auto &w) {
                        tx_checks.byron_signers.emplace_back(w);
                    });
                    tx.foreach_witness_shelley_vkey([&](const auto &w) {
                        tx_checks.signers.emplace(vkey_signer_t { crypto::blake2b::digest<key_hash>(w.vkey) });
                    });
                    tx.foreach_witness_shelley_bootstrap([&](const auto &w) {
                        crypto::ed25519::vkey_full vk_full {};
                        if (sizeof(vk_full) != sizeof(w.vkey) + w.chain_code.size()) [[unlikely]]
                            throw error(fmt::format("invalid chain code size: {} (expected: {})", w.chain_code.size(), sizeof(vk_full) - sizeof(w.vkey)));
                        memcpy(vk_full.data(), w.vkey.data(), w.vkey.size());
                        memcpy(vk_full.data() + w.vkey.size(), w.chain_code.data(), w.chain_code.size());
                        tx_checks.signers.emplace(bootstrap_signer_t { byron_addr_root_hash(0, vk_full, w.attrs) });
                    });
                    tx.foreach_mint([&](const auto &policy_id, const auto &assets) {
                        bool minted = false;
                        for (const auto &[name, diff]: assets) {
                            // negative mint values signify an outflow of tokens
                            if (diff < 0) {
                                tx_checks.balances.out_assets[policy_id][name] += -diff;
                                minted = true;
                            } else if (diff > 0) {
                                tx_checks.balances.in_assets[policy_id][name] += diff;
                                minted = true;
                            }
                        }
                        if (minted)
                            tx_checks.required_signers.emplace(script_signer_t { policy_id, redeemer_tag::mint });
                    });
                    part.txs[tx_part_idx].emplace_back(std::move(tx_checks));
                });
                blk->foreach_invalid_tx([&](const auto &tx) {
                    ++stats.num_invalid_txs;
                    tx.foreach_collateral([&](const auto &txi) {
                        part.timed_updates.emplace_back(make_timed_update(
                            cert_loc_t { blk->slot(), tx.index(), 0 },
                            index::timed_update::collected_collateral_input { txi.hash, txi.idx }
                        ));
                    });
                    if (const auto *babbage_tx = dynamic_cast<const cardano::babbage::tx_base *>(&tx); babbage_tx) {
                        if (const auto c_ret = babbage_tx->collateral_return(); c_ret) {
                            part.timed_updates.emplace_back(make_timed_update(
                                cert_loc_t { blk->slot(), tx.index(), 0 },
                                index::timed_update::collected_collateral_refund { c_ret->coin }
                            ));
                            _add_utxo(part.utxos, tx, *c_ret, tx.outputs().size());
                        }
                    }
                });
                part.block_updates.emplace_back(blk->slot(), blk->issuer_hash(), fees, donations,
                    blk.end_offset(), numeric_cast<uint8_t>(blk->era()));
            }

            wit_cnt witnesses_ok_stage1(const block_container &blk, const tx_base &tx) const
            {
                const bool first_slot_ok = !intersection || blk.offset() >= intersection->end_offset;
                const bool last_slot_ok = !to || blk.offset() < to->end_offset;
                if (first_slot_ok && last_slot_ok) {
                    try {
                        switch (typ) {
                            case witness_type::all: {
                            case witness_type::vkey:
                                signer_set valid_vkeys {};
                                auto cnts = tx.witnesses_ok_vkey(valid_vkeys);
                                cnts += tx.witnesses_ok_native(valid_vkeys);
                                return cnts;
                            }
                            case witness_type::script:
                            case witness_type::none:
                                return {};
                            default: throw error(fmt::format("unsupported witness type: {}", static_cast<int>(typ)));
                        }
                    } catch (const std::exception &ex) {
                        const auto msg = fmt::format("txwit: slot: {} tx: {} error: {}", blk->slot(), tx.hash(), ex.what());
                        logger::error("{}", msg);
                        error_handler(msg);
                    }
                }
                return {};
            }

            wit_cnt witnesses_ok_stage2(const block_base &blk, const tx_base &tx, const context &ctx) const
            {
                const bool first_slot_ok = !intersection || blk.offset() >= intersection->end_offset;
                const bool last_slot_ok = !to || blk.offset() < to->end_offset;
                wit_cnt cnts {};
                if (first_slot_ok && last_slot_ok) {
                    try {
                        switch (typ) {
                            case witness_type::all:
                            case witness_type::script:
                                cnts += tx.witnesses_ok_plutus(ctx);
                                break;
                            default:
                                break;
                        }
                    } catch (const std::exception &ex) {
                        const auto msg = fmt::format("txwit slot: {} tx: {} error: {}", blk.slot(), tx.hash(), ex.what());
                        logger::error("{}", msg);
                        error_handler(msg);
                    }
                }
                return cnts;
            }
        };

        // This component keeps its own copy of the ledger state
        // since some checks require the knowledge of the actual protocol parameters or if a given certificate is registered.
        // Therefore, the state processing is limited here. That is OK.
        // The full ledger state processing happens in the consensus validation in lib/dt/validator.cpp
        // The two pieces will be merged  after the transaction witness validation has been tested for enough time.

        struct stage2_processor {
            stage2_processor(const chunk_registry &cr, const validation_config_t &cfg): _cr { cr }, _cfg { cfg }
            {
                _st.start_epoch(0);
                if (_cfg.intersection) {
                    const auto *snap = _cr.validator().snapshots().best([&](const auto &s) { return s.end_offset <=  _cfg.intersection->end_offset; });
                    if (snap)
                        _cr.validator().load_snapshot(_st, *snap);
                }
                _refresh_cost_models();
            }

            void apply_batch(batch_info &&part)
            {
                _apply_epoch_update(part);
                auto update_effects = _apply_ledger_updates_before_witnesses(part);
                _cnts += _validate_witnesses_and_invariants(part);
                part.tx_frames.clear();
                _apply_ledger_updates_after_witnesses(part, std::move(update_effects));
            }

            const wit_cnt &counts() const
            {
                return _cnts;
            }

            size_t errors() const
            {
                return _num_errs;
            }

            std::vector<storage::chunk_cptr_list> prepare_batches() const
            {
                std::vector<storage::chunk_cptr_list> batches {};
                storage::chunk_cptr_list chunks {};
                for (const auto &[last_byte_offset, info]: _cr.chunks()) {
                    if (info.end_offset() > _st.end_offset() && (!_cfg.to || info.first_slot <= _cfg.to->slot))
                        chunks.emplace_back(&info);
                }
                logger::info("txwit: matched chunks for processing: {}", chunks.size());
                if (!chunks.empty()) {
                    if (chunks.front()->offset != _st.end_offset()) [[unlikely]]
                        throw error("internal error: failed to match available chunks with ledger snapshots");
                    logger::info("txwit: first chunk to be processed at offset: {} state end_offset: {}",
                        chunks.front()->offset, _st.end_offset());
                }

                // Bigger batches provide for a higher total throughput
                // However that requires more RAM makes the tasks occupy the scheduler workers for longer,
                // which is a problem for latency sensitive tasks of the second stage
                static constexpr size_t batch_size = 2;
                storage::chunk_cptr_list batch {};
                for (const auto *chunk: chunks) {
                    // ensure that each batch has between 1 and batch_size elements and all from the same epoch
                    if (batch.size() == batch_size || (!batch.empty() && _cr.make_slot(batch.front()->first_slot).epoch() != _cr.make_slot(chunk->first_slot).epoch())) {
                        batches.emplace_back(std::move(batch));
                        batch.clear();
                    }
                    batch.emplace_back(chunk);
                }
                if (!batch.empty())
                    batches.emplace_back(std::move(batch));
                logger::debug("txwit: prepared chunk batches: {}", batches.size());
                return batches;
            }
        private:
            const chunk_registry &_cr;
            const validation_config_t &_cfg;
            state _st {};
            plutus_cost_models _cost_models_raw = _st.params().plutus_cost_models;
            costs::runtime_models _cost_models = costs::ingest(_cost_models_raw);
            wit_cnt _cnts {};
            size_t _num_errs = 0;

            void _refresh_cost_models()
            {
                if (_st.params().plutus_cost_models != _cost_models_raw) {
                    _cost_models_raw = _st.params().plutus_cost_models;
                    _cost_models = costs::ingest(_cost_models_raw);
                }
            }

            void _apply_epoch_update(const batch_info &part)
            {
                if (part.epoch > _st.epoch()) {
                    timer t { fmt::format("txwit batch: {} epoch: {} apply_epoch_update", part.part_id, part.epoch), logger::level::debug };
                    if (part.epoch != _st.epoch() + 1) [[unlikely]]
                        throw error(fmt::format("unexpected epoch: {} after: {}", part.epoch, _st.epoch()));
                    _st.start_epoch(part.epoch);
                    _refresh_cost_models();
                }
            }

            void _observe_deposit_effects(batch_info &part, const timed_update_info_t &upd) const
            {
                if (!upd.has_tx_loc)
                    return;
                std::visit([&](const auto &c) {
                    using T = std::decay_t<decltype(c)>;
                    if constexpr (std::is_same_v<T, stake_reg_cert>) {
                        if (!_st.has_stake(c.stake_id))
                            part.tx_deposits[upd.tx_loc].out_coin += _st.params().key_deposit;
                    } else if constexpr (std::is_same_v<T, reg_cert>
                            || std::is_same_v<T, stake_reg_deleg_cert>
                            || std::is_same_v<T, vote_reg_deleg_cert>
                            || std::is_same_v<T, stake_vote_reg_deleg_cert>) {
                        if (!_st.has_stake(c.stake_id))
                            part.tx_deposits[upd.tx_loc].out_coin += c.deposit;
                    } else if constexpr (std::is_same_v<T, stake_dereg_cert>) {
                        part.tx_deposits[upd.tx_loc].in_coin += _st.params().key_deposit;
                    } else if constexpr (std::is_same_v<T, unreg_cert>) {
                        part.tx_deposits[upd.tx_loc].in_coin += c.deposit;
                    } else if constexpr (std::is_same_v<T, pool_reg_cert>) {
                        if (!_st.has_pool(c.pool_id))
                            part.tx_deposits[upd.tx_loc].out_coin += _st.params().pool_deposit;
                    } else if constexpr (std::is_same_v<T, reg_drep_cert>) {
                        if (!_st.has_drep(c.drep_id))
                            part.tx_deposits[upd.tx_loc].out_coin += c.deposit;
                    } else if constexpr (std::is_same_v<T, unreg_drep_cert>) {
                        part.tx_deposits[upd.tx_loc].in_coin += c.deposit;
                    }
                }, upd.update.update);
            }

            update_effects_t _apply_ledger_updates_before_witnesses(batch_info &part)
            {
                timer t { fmt::format("txwit batch: {} epoch: {} seq apply ledger updates before witnesses", part.part_id, part.epoch), logger::level::debug };
                const auto rss_before_mb = memory::my_usage_mb();
                const auto num_block_updates = part.block_updates.size();
                const auto num_timed_updates = part.timed_updates.size();
                block_update_list block_updates {};
                block_updates.reserve(part.block_updates.size());
                for (auto &&upd: part.block_updates)
                    block_updates.emplace_back(std::move(upd));
                auto stage_start = diagnostic_clock::now();
                _st.process_block_updates(std::move(block_updates));
                const auto block_updates_ns = _diagnostic_elapsed_ns(stage_start);

                update_effects_t effects {};
                stage_start = diagnostic_clock::now();
                for (auto &upd: part.timed_updates) {
                    _observe_deposit_effects(part, upd);
                    _st.process_timed_update(effects, std::move(upd.update));
                }
                const auto timed_updates_ns = _diagnostic_elapsed_ns(stage_start);
                logger::debug(
                    "txwit pre-ledger diagnostics batch: {} epoch: {} block_updates: {} timed_updates: {} "
                    "block_updates_ms: {:.3f} timed_updates_ms: {:.3f} rss_before_mb: {} rss_after_mb: {} peak_rss_mb: {}",
                    part.part_id, part.epoch, num_block_updates, num_timed_updates,
                    _diagnostic_ms(block_updates_ns), _diagnostic_ms(timed_updates_ns),
                    rss_before_mb, memory::my_usage_mb(), memory::max_usage_mb());
                return effects;
            }

            void _apply_ledger_updates_after_witnesses(batch_info &part, update_effects_t &&effects)
            {
                timer t { fmt::format("txwit batch: {} epoch: {} par apply ledger updates after witnesses", part.part_id, part.epoch), logger::level::debug };
                const auto rss_before_mb = memory::my_usage_mb();
                const auto num_utxo_updates = part.utxos.size();
                size_t nonempty_utxo_parts = 0;
                for (size_t pi = 0; pi < txo_map::num_parts; ++pi) {
                    if (!part.utxos.partition(pi).empty())
                        ++nonempty_utxo_parts;
                }
                utxo_update_list utxo_updates {};
                utxo_updates.emplace_back(std::move(part.utxos));
                auto stage_start = diagnostic_clock::now();
                _st.process_utxo_updates(std::move(utxo_updates));
                const auto process_utxos_ns = _diagnostic_elapsed_ns(stage_start);
                stage_start = diagnostic_clock::now();
                _st.finish_update_processing(std::move(effects), part.finalize_after_batch);
                const auto finish_updates_ns = _diagnostic_elapsed_ns(stage_start);
                const auto state_sizes = _st.container_sizes();
                logger::debug(
                    "txwit post-ledger diagnostics batch: {} epoch: {} utxo_updates: {} nonempty_utxo_parts: {} finalize_after_batch: {} "
                    "process_utxos_ms: {:.3f} finish_updates_ms: {:.3f} state_utxos: {} state_accounts: {} state_pointers: {} "
                    "state_snapshot_entries: {} state_reward_entries: {} state_delegation_entries: {} state_pool_entries: {} "
                    "rss_before_mb: {} rss_after_mb: {} peak_rss_mb: {}",
                    part.part_id, part.epoch, num_utxo_updates, nonempty_utxo_parts, part.finalize_after_batch,
                    _diagnostic_ms(process_utxos_ns), _diagnostic_ms(finish_updates_ns),
                    state_sizes.utxos, state_sizes.accounts, state_sizes.pointers,
                    state_sizes.snapshot_entries, state_sizes.reward_entries,
                    state_sizes.delegation_entries, state_sizes.pool_entries,
                    rss_before_mb, memory::my_usage_mb(), memory::max_usage_mb());
            }

            static cpp_int _floor_nonnegative(const cpp_rational &value)
            {
                return numerator(value) / denominator(value);
            }

            static cpp_int _ceil_nonnegative(const cpp_rational &value)
            {
                const auto &num = numerator(value);
                const auto &den = denominator(value);
                return (num + den - 1) / den;
            }

            cpp_int _reference_script_fee(size_t script_size) const
            {
                static constexpr size_t tier_size = 25'600;
                const cpp_rational multiplier { 6, 5 };
                auto tier_price = rational_from_r64(_st.params().min_fee_ref_script_cost_per_byte);
                cpp_rational accumulated = 0;
                while (script_size >= tier_size) {
                    accumulated += tier_size * tier_price;
                    tier_price *= multiplier;
                    script_size -= tier_size;
                }
                accumulated += script_size * tier_price;
                return _floor_nonnegative(accumulated);
            }

            cpp_int _minimum_fee(
                const uint32_t tx_size,
                const uint8_t era,
                const ex_units &declared_ex_units,
                const size_t ref_script_size) const
            {
                cpp_int min_fee = cpp_int { _st.params().min_fee_a } * tx_size + _st.params().min_fee_b;
                const auto script_fee = cpp_rational { declared_ex_units.mem }
                        * rational_from_r64(_st.params().ex_unit_prices.mem)
                    + cpp_rational { declared_ex_units.steps }
                        * rational_from_r64(_st.params().ex_unit_prices.steps);
                min_fee += _ceil_nonnegative(script_fee);
                if (era >= 7)
                    min_fee += _reference_script_fee(ref_script_size);
                return min_fee;
            }

            void _validate_reference_scripts(batch_info &part) const
            {
                timer t { fmt::format("txwit batch: {} epoch: {} validate reference scripts", part.part_id, part.epoch), logger::level::debug };
                static constexpr size_t max_ref_script_size_per_tx = 200 * 1024;
                static constexpr size_t max_ref_script_size_per_block = 1024 * 1024;
                part.ref_script_sizes.clear();
                const auto protocol_major = _st.params().protocol_ver.major;
                if (protocol_major < 9)
                    return;
                if (part.ref_script_parts.size() != batch_info::num_parts) [[unlikely]]
                    throw error(fmt::format(
                        "invalid number of reference-script join partitions: {} (expected: {})",
                        part.ref_script_parts.size(), batch_info::num_parts));
                size_t num_uses = 0;
                size_t num_produced = 0;
                size_t num_consumed = 0;
                for (const auto &ref_part: part.ref_script_parts) {
                    num_uses += ref_part.uses.size();
                    num_produced += ref_part.produced.size();
                    num_consumed += ref_part.consumed.size();
                }
                const auto rss_before_mb = memory::my_usage_mb();

                // A TXO can be referenced from several hash partitions, so each partition
                // contributes independently to the transaction totals. The batch-local
                // producer position supplies the ordering that a serial UTXO replay used to
                // provide. Existing ledger TXOs precede every position in this batch.
                const auto num_txs = part.ref_script_txs.size();
                auto ledger_script_sizes = std::make_unique<std::atomic_size_t[]>(num_txs);
                auto block_script_sizes = std::make_unique<std::atomic_size_t[]>(num_txs);
                for (size_t ti = 0; ti < num_txs; ++ti) {
                    ledger_script_sizes[ti].store(0, std::memory_order_relaxed);
                    block_script_sizes[ti].store(0, std::memory_order_relaxed);
                }

                static const std::string task_id { "validate-reference-scripts" };
                auto &sched = _cr.sched();
                const auto join_start = diagnostic_clock::now();
                sched.wait_all(task_id, [&](const auto &, const auto &submit_f) {
                    for (size_t pi = 0; pi < batch_info::num_parts; ++pi) {
                        submit_f({ 2000, task_id, [&, pi] {
                            const auto &ref_part = part.ref_script_parts[pi];
                            const auto &utxo_part = _st.utxos().partition(pi);
                            for (const auto &use: ref_part.uses) {
                                if (use.tx_info_idx >= num_txs) [[unlikely]]
                                    throw error(fmt::format(
                                        "invalid reference-script transaction index: {} (number of transactions: {})",
                                        use.tx_info_idx, num_txs));
                                const auto &tx = part.ref_script_txs[use.tx_info_idx];

                                const auto produced_it = std::lower_bound(
                                    ref_part.produced.begin(), ref_part.produced.end(), use.id,
                                    [](const ref_script_production_t &entry, const tx_out_ref &id) {
                                        return entry.id < id;
                                    });
                                const auto *produced = produced_it != ref_part.produced.end() && produced_it->id == use.id
                                    ? &*produced_it
                                    : nullptr;
                                const auto consumed_it = std::lower_bound(
                                    ref_part.consumed.begin(), ref_part.consumed.end(), use.id,
                                    [](const ref_script_consumption_t &entry, const tx_out_ref &id) {
                                        return entry.id < id;
                                    });
                                const auto *consumed = consumed_it != ref_part.consumed.end() && consumed_it->id == use.id
                                    ? &*consumed_it
                                    : nullptr;
                                const auto ledger_it = utxo_part.find(use.id);
                                const auto *ledger_txo = ledger_it != utxo_part.end()
                                    ? &ledger_it->second
                                    : nullptr;

                                // A collision with an existing UTXO would make a single
                                // producer index ambiguous and is invalid independently of
                                // reference-script accounting.
                                if (produced && ledger_txo) [[unlikely]]
                                    throw error(fmt::format(
                                        "reference-script accounting found duplicate TXO {}",
                                        use.id));

                                size_t ledger_script_size = 0;
                                if (produced && produced->pos < tx.pos) {
                                    if (!consumed || !(consumed->pos < tx.pos))
                                        ledger_script_size = produced->script_size;
                                    else
                                        throw error(fmt::format(
                                            "reference-script accounting references an unavailable TXO {}",
                                            use.id));
                                } else if (ledger_txo) {
                                    if (!consumed || !(consumed->pos < tx.pos)) {
                                        if (ledger_txo->script_ref)
                                            ledger_script_size = ledger_txo->script_ref->script().size();
                                    } else {
                                        throw error(fmt::format(
                                            "reference-script accounting references an unavailable TXO {}",
                                            use.id));
                                    }
                                } else {
                                    throw error(fmt::format(
                                        "reference-script accounting references an unavailable TXO {}",
                                        use.id));
                                }

                                size_t block_script_size = 0;
                                if (produced) {
                                    // BBODY <= PV10 sees the UTXO at block start. PV11 also
                                    // sees scripts produced by an earlier transaction in the
                                    // current block. A same-block spend does not remove an
                                    // item from that accounting view.
                                    if (produced->pos.block_height < tx.pos.block_height) {
                                        if (!consumed || consumed->pos.block_height >= tx.pos.block_height)
                                            block_script_size = produced->script_size;
                                    } else if (protocol_major >= 11
                                            && produced->pos.block_height == tx.pos.block_height
                                            && produced->pos.tx_idx < tx.pos.tx_idx) {
                                        block_script_size = produced->script_size;
                                    }
                                } else if (ledger_txo) {
                                    if (!consumed || consumed->pos.block_height >= tx.pos.block_height) {
                                        if (ledger_txo->script_ref)
                                            block_script_size = ledger_txo->script_ref->script().size();
                                    }
                                }

                                ledger_script_sizes[use.tx_info_idx].fetch_add(
                                    ledger_script_size, std::memory_order_relaxed);
                                block_script_sizes[use.tx_info_idx].fetch_add(
                                    block_script_size, std::memory_order_relaxed);
                            }
                        }});
                    }
                });
                const auto join_ns = _diagnostic_elapsed_ns(join_start);

                const auto reduce_start = diagnostic_clock::now();
                timer reduce_timer { fmt::format("txwit batch: {} epoch: {} seq reduce reference scripts", part.part_id, part.epoch), logger::level::debug };
                for (const auto &block: part.ref_script_blocks) {
                    const auto block_tx_end = static_cast<size_t>(block.first_tx_idx) + block.num_txs;
                    if (block_tx_end > num_txs) [[unlikely]]
                        throw error(fmt::format(
                            "invalid reference-script block transaction range: {}..{} (number of transactions: {})",
                            block.first_tx_idx, block_tx_end, num_txs));
                    size_t block_script_size = 0;
                    for (size_t ti = block.first_tx_idx; ti < block_tx_end; ++ti) {
                        const auto &tx = part.ref_script_txs[ti];
                        if (tx.pos.block_height != block.height) [[unlikely]]
                            throw error(fmt::format(
                                "reference-script transaction height {} does not match block height {}",
                                tx.pos.block_height, block.height));
                        if (protocol_major < 11 && tx.regular_ref_overlap) {
                            throw error(fmt::format(
                                "slot {} tx {} uses TXO {} as both a regular and a reference input",
                                block.slot,
                                tx.tx_id,
                                *tx.regular_ref_overlap));
                        }
                        const auto ledger_tx_script_size = ledger_script_sizes[ti].load(std::memory_order_relaxed);
                        if (tx.phase2_valid && ledger_tx_script_size > max_ref_script_size_per_tx) {
                            throw error(fmt::format(
                                "slot {} tx {} reference scripts have size {} exceeding the per-transaction limit {}",
                                block.slot,
                                tx.tx_id,
                                ledger_tx_script_size,
                                max_ref_script_size_per_tx));
                        }
                        block_script_size += block_script_sizes[ti].load(std::memory_order_relaxed);
                        part.ref_script_sizes[tx.tx_id] = ledger_tx_script_size;
                    }
                    if (block_script_size > max_ref_script_size_per_block) {
                        throw error(fmt::format(
                            "slot {} reference scripts have total size {} exceeding the per-block limit {}",
                            block.slot,
                            block_script_size,
                            max_ref_script_size_per_block));
                    }
                }
                logger::debug(
                    "txwit reference-script diagnostics batch: {} epoch: {} blocks: {} txs: {} uses: {} produced: {} consumed: {} "
                    "join_ms: {:.3f} reduce_ms: {:.3f} rss_before_mb: {} rss_after_mb: {} peak_rss_mb: {}",
                    part.part_id, part.epoch, part.ref_script_blocks.size(), part.ref_script_txs.size(),
                    num_uses, num_produced, num_consumed, _diagnostic_ms(join_ns),
                    _diagnostic_ms(_diagnostic_elapsed_ns(reduce_start)), rss_before_mb,
                    memory::my_usage_mb(), memory::max_usage_mb());
            }

            std::unique_ptr<context> _prep_plutus_ctx(tx_context_t &tx) const
            {
                const auto &utxos = _st.utxos();
                tx.native_script_refs.reserve(tx.inputs.size() + tx.ref_inputs.size());
                // process inputs before they are moved into the plutus::context
                for (auto &[id, data]: tx.inputs) {
                    if (!data) {
                        const auto it = utxos.find(id);
                        if (it == utxos.end()) [[unlikely]]
                            throw error(fmt::format("tx {} references an unknown TXO {}!", tx.tx_id, id));
                        data = it->second;
                    }
                    tx.balances.in_coin += data.coin;
                    for (const auto &[policy_id, assets]: data.assets) {
                        for (const auto &[name, coin]: assets) {
                            if (coin)
                                tx.balances.in_assets[policy_id][name] += coin;
                        }
                    }
                    if (data.script_ref) {
                        if (data.script_ref->type() == script_type::native)
                            tx.native_script_refs.try_emplace(data.script_ref->hash(), *data.script_ref);
                    }
                }
                for (auto &[id, data]: tx.ref_inputs) {
                    if (!data) {
                        const auto it = utxos.find(id);
                        if (it == utxos.end()) [[unlikely]]
                            throw error(fmt::format("tx {} references an unknown TXO {}!", tx.tx_id, id));
                        data = it->second;
                    }
                    if (data.script_ref) {
                        if (data.script_ref->type() == script_type::native)
                            tx.native_script_refs.try_emplace(data.script_ref->hash(), *data.script_ref);
                    }
                }
                if (tx.plutus_ctx) {
                    const auto ledger_protocol_ver = _st.params().protocol_ver;
                    tx.plutus_ctx->protocol_ver = ledger_protocol_ver;
                    tx.plutus_ctx->validate_format();
                    auto p_ctx = std::make_unique<context>(
                        std::move(tx.plutus_ctx->body), std::move(tx.plutus_ctx->wits),
                        tx.plutus_ctx->block, _cr.config()
                    );
                    p_ctx->protocol_ver(ledger_protocol_ver);
                    tx.plutus_ctx.reset();
                    p_ctx->set_inputs(std::move(tx.inputs), std::move(tx.ref_inputs));
                    return p_ctx;
                }
                return nullptr;
            }

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

            void _validate_shelley_tx_invariants(const batch_info &part, tx_context_t &tx, const context *plutus_ctx) const
            {
                if (const auto it = part.tx_deposits.find(tx.tx_loc); it != part.tx_deposits.end()) {
                    tx.balances.in_coin += it->second.in_coin;
                    tx.balances.out_coin += it->second.out_coin;
                }
                if (!tx.balances.match()) [[unlikely]]
                    throw error(fmt::format("tx {}: consumed != produced: {}", tx.tx_id, tx.balances));
                if (plutus_ctx) {
                    plutus_ctx->validate_redeemer_budgets(_st.params().max_tx_ex_units);
                    for (const auto &[rid, rdata]: plutus_ctx->redeemers())
                        tx.signers.emplace(script_signer_t { plutus_ctx->redeemer_script(rid), rid.tag });
                }
                std::optional<signer_set> vkey_signers {};
                for (const auto &rs: tx.required_signers) {
                    if (tx.signers.contains(rs))
                        continue;
                    if (std::holds_alternative<script_signer_t>(rs.val)) {
                        const auto &s_hash = std::get<script_signer_t>(rs.val).hash;
                        if (tx.native_scripts.contains(s_hash))
                            continue;
                        // validate references native scripts here since they do not have their own tx_witness entries
                        // the optional in s_it->second has value only the first time a given script is referenced
                        if (const auto s_it = tx.native_script_refs.find(s_hash); s_it != tx.native_script_refs.end() && s_it->second) {
                            if (!vkey_signers) {
                                vkey_signers.emplace();
                                vkey_signers->reserve(tx.signers.size());
                                for (const auto &s: tx.signers) {
                                    if (std::holds_alternative<vkey_signer_t>(s.val))
                                        vkey_signers->emplace(std::get<vkey_signer_t>(s.val).hash);
                                }
                                const auto &script = *s_it->second;
                                if (const auto err = native_script::validate(cbor::zero2::parse(script.script()).get(), tx.slot, *vkey_signers); err) [[unlikely]]
                                    throw error(fmt::format("native script: {} failed to validate tx {}: {}", script.hash(), tx.tx_id, err));
                            }
                            s_it->second.reset();
                            continue;
                        }
                    }
                    throw error(fmt::format("epoch: {} tx {} missing a required_signer: {}", part.epoch, tx.tx_id, rs));
                }
                if (tx.reqires_genesis_delegs_quorum) [[unlikely]] {
                    size_t num_signers = 0;
                    for (const auto &vk_hash: _cr.config().byron_delegate_hashes) {
                        if (tx.signers.contains({ vkey_signer_t { vk_hash } }))
                            ++num_signers;
                    }
                    const auto quorum = _cr.config().shelley_update_quorum;
                    if (num_signers < quorum) [[unlikely]]
                        throw error(fmt::format("a quorum of {} genesis delegates is required but got only: {}", quorum, num_signers));
                    logger::debug("epoch: {} tx: {} requires a quorum of {} genesis delegates and got {}",
                        part.epoch, tx.tx_id, quorum, num_signers);
                }
            }

            void _validate_tx_invariants(const batch_info &part, tx_context_t &tx, const context *plutus_ctx) const
            {
                try {
                    ex_units declared_ex_units {};
                    if (plutus_ctx)
                        declared_ex_units = plutus_ctx->validate_redeemer_budgets(_st.params().max_tx_ex_units);
                    const auto ref_script_size = tx.era >= 7
                        ? part.ref_script_sizes.at(tx.tx_id)
                        : 0;
                    const auto min_fee = _minimum_fee(
                        tx.tx_size,
                        tx.era,
                        declared_ex_units,
                        ref_script_size);
                    if (cpp_int { tx.fee } < min_fee) [[unlikely]]
                        throw error(fmt::format("epoch: {} tx {} an insufficient fee for a tx of size {}: {} < {}",
                            part.epoch, tx.tx_id, static_cast<size_t>(tx.tx_size), tx.fee, min_fee));
                    switch (tx.era) {
                        case 0:
                            throw error(fmt::format("transaction {} in era 0!", tx.tx_id));
                        case 1:
                            _validate_byron_tx_invariants(part, tx);
                            break;
                        default:
                            _validate_shelley_tx_invariants(part, tx, plutus_ctx);
                            break;
                    }
                } catch (const std::exception &ex) {
                    const auto msg = fmt::format("txwit epoch: {} tx: {}: {}", part.epoch, tx.tx_id, ex.what());
                    logger::error("{}", msg);
                    _cfg.error_handler(msg);
                }
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
                std::vector<validation_partition_diagnostics_t> diagnostics(batch_info::num_parts);
                sched.wait_all(task_id, [&](const auto &, const auto &submit_f) {
                    for (size_t pi = 0; pi < batch_info::num_parts; ++pi) {
                        submit_f({ 2000, task_id, [&, pi] {
                            const auto task_start = diagnostic_clock::now();
                            auto &diag = diagnostics[pi];
                            wit_cnt batch_cnts {};
                            auto txs = _diagnostic_decode_zstd<std::vector<tx_context_t>>(part.tx_frames[pi], diag.io);
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
                                    plutus_ctx->cost_models(_cost_models);
                                    stage_start = diagnostic_clock::now();
                                    plutus_ctx->prepare();
                                    diag.plutus_prepare_ns += _diagnostic_elapsed_ns(stage_start);
                                    const auto &tx = plutus_ctx->tx();
                                    stage_start = diagnostic_clock::now();
                                    batch_cnts += _cfg.witnesses_ok_stage2(tx.block(), tx, *plutus_ctx);
                                    diag.plutus_evaluate_ns += _diagnostic_elapsed_ns(stage_start);
                                }
                            }
                            mutex::scoped_lock lk { part_mutex };
                            cnts += batch_cnts;
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

            void _validate_max_stats(const batch_info &part) const
            {
                const auto &max = part.max_stats;
                if (max.max_block_body_size && *max.max_block_body_size > _st.params().max_block_body_size) [[unlikely]]
                    throw error(fmt::format("block body size of {} exceeded the limit of {}", *max.max_block_body_size, _st.params().max_block_body_size));
                if (max.max_block_header_size && *max.max_block_header_size > _st.params().max_block_header_size) [[unlikely]]
                    throw error(fmt::format("block header size of {} exceeded the limit of {}", *max.max_block_header_size, _st.params().max_block_header_size));
                if (max.max_tx_size && *max.max_tx_size > _st.params().max_transaction_size) [[unlikely]]
                    throw error(fmt::format("tx size of {} exceeded the limit of {}", *max.max_tx_size, _st.params().max_transaction_size));
            }

            wit_cnt _validate_witnesses_and_invariants(batch_info &part)
            {
                _validate_max_stats(part);
                _validate_reference_scripts(part);
                return _validate_witnesses(part);
            }
        };

        const chunk_registry &_cr;
        const validation_config_t _cfg;

        static batch_stats_t _process_batch_stage1(const chunk_registry &cr, const size_t batch_no,
            const storage::chunk_cptr_list &batch, const validation_config_t &cfg, const bool finalize_after_batch,
            std::unique_ptr<batch_info> &handoff)
        {
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
                    "active_stage1_after: {} peak_active_stage1: {} ready_batches: {} peak_ready_batches: {}",
                    batch_no, diag.epoch, diag.complete, diag.timed_updates, diag.utxo_updates, diag.ref_script_uses,
                    diag.rss_start_mb, diag.rss_during_parse_max_mb, diag.rss_after_parse_mb, diag.rss_after_tx_frames_mb,
                    memory::my_usage_mb(), memory::max_usage_mb(), active_stage1,
                    active_after, pipeline_diag.max_active_stage1.load(std::memory_order_relaxed), diag.ready_batches,
                    pipeline_diag.max_ready_batches.load(std::memory_order_relaxed));
            }};
            batch_info part { batch_no, cr.make_slot(batch.front()->first_slot).epoch() };
            part.finalize_after_batch = finalize_after_batch;
            for (const auto *chunk_ptr: batch) {
                const auto &chunk = *chunk_ptr;
                const auto first_epoch = cr.make_slot(chunk.first_slot).epoch();
                const auto last_epoch = cr.make_slot(chunk.last_slot).epoch();
                if (first_epoch != part.epoch || last_epoch != part.epoch) [[unlikely]]
                    throw error(fmt::format("batch: {} contains data from multiple epochs: {}, {}, {}", batch_no, part.epoch, first_epoch, last_epoch));

                const auto canon_path = cr.full_path(chunk.rel_path());
                uint8_vector data {};
                {
                    auto io_start = diagnostic_clock::now();
                    const auto compressed = file::read(canon_path);
                    diag.input_read_ns += _diagnostic_elapsed_ns(io_start);
                    diag.input_compressed_bytes += compressed.size();
                    io_start = diagnostic_clock::now();
                    zstd::decompress(data, compressed);
                    diag.input_decompress_ns += _diagnostic_elapsed_ns(io_start);
                }
                diag.input_serialized_bytes += data.size();

                const auto parse_start = diagnostic_clock::now();
                cbor::zero2::decoder dec { data };
                while (!dec.done()) {
                    auto &block_tuple = dec.read();
                    ++diag.blocks;
                    const block_container blk { numeric_cast<uint64_t>(chunk.offset + block_tuple.data_begin() - data.data()), block_tuple, cr.config() };
                    // Byron epoch boundary blocks contain no information and therefore are skipped
                    if (blk->era() > 0) [[likely]] {
                        try {
                            cfg.pre_aggregate_data(part, blk);
                        } catch (const std::exception &ex) {
                            throw error(fmt::format("failed to parse block at slot: {} hash: {}: {}", blk->slot_object(), blk->hash(), ex.what()));
                        }
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
            for (size_t pi = 0; pi < batch_info::num_parts; ++pi) {
                zstd_io_diagnostics_t tx_io_diag {};
                part.tx_frames[pi] = _diagnostic_encode_zstd(part.txs[pi], tx_io_diag);
                diag.tx_frame_compressed_bytes += tx_io_diag.compressed_bytes;
                diag.tx_frame_serialized_bytes += tx_io_diag.serialized_bytes;
                diag.tx_serialize_ns += tx_io_diag.serialize_ns;
                diag.tx_compress_ns += tx_io_diag.compress_ns;
                std::vector<tx_context_t> {}.swap(part.txs[pi]);
            }
            diag.rss_after_tx_frames_mb = memory::my_usage_mb();
            const auto stats = part.stats;
            // The transaction contexts have been replaced by compressed frames.
            part.txs.clear();
            handoff = std::make_unique<batch_info>(std::move(part));
            diag.ready_batches = pipeline_diag.ready_batches.fetch_add(1, std::memory_order_relaxed) + 1;
            _diagnostic_update_max(pipeline_diag.max_ready_batches, diag.ready_batches);
            diag.complete = true;
            return stats;
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

        static std::filesystem::path _batch_dir(const chunk_registry &cr, const size_t batch_id)
        {
            return cr.data_dir() / "txwit" / fmt::format("batch-{:05}", batch_id);
        }

        static std::string _batch_path(const chunk_registry &cr, const size_t batch_id, const std::string_view suffix)
        {
            return (_batch_dir(cr, batch_id)/ fmt::format("{}.zpp", suffix) ).string();
        }

        static void _dump_tx_frames(const chunk_registry &cr, const batch_info &part)
        {
            if (part.tx_frames.size() != batch_info::num_parts) [[unlikely]]
                throw error(fmt::format(
                    "cannot dump {} transaction frames for batch {} (expected: {})",
                    part.tx_frames.size(), part.part_id, batch_info::num_parts));
            size_t compressed_bytes = 0;
            for (size_t pi = 0; pi < part.tx_frames.size(); ++pi) {
                const auto &frame = part.tx_frames[pi];
                compressed_bytes += frame.size();
                file::write(_batch_path(cr, part.part_id, fmt::format("txs-{:02X}", pi)), frame);
            }
            logger::warn(
                "dumped transaction frames for failed txwit batch {} to {} compressed_mib: {:.2f}",
                part.part_id, _batch_dir(cr, part.part_id), _diagnostic_mib(compressed_bytes));
        }

        batch_stats_t _process_batches(parallel::ordered_consumer &part_c, const std::vector<storage::chunk_cptr_list> &batches,
            std::vector<std::unique_ptr<batch_info>> &batch_handoff,
            std::function<void(size_t)> &replenish_stage1) const
        {
            mutex::unique_lock::mutex_type all_mutex alignas(mutex::alignment) {};
            batch_stats_t all {};
            auto &sched = _cr.sched();
            static const std::string task_id { "parse" };
            static constexpr size_t run_ahead_worker_multiplier = 1;
            // Bound submitted work, rather than blocking completed parse tasks, so
            // validation and ledger work can always claim workers as they become free.
            const size_t max_run_ahead = std::max<size_t>(1, sched.num_workers() * run_ahead_worker_multiplier);
            std::atomic_size_t next_batch_to_submit { 0 };
            std::atomic_bool stop_submitting { false };
            scope_exit stop_replenishing { [&] {
                stop_submitting.store(true, std::memory_order_release);
                replenish_stage1 = {};
            }};
            const auto ex_ptr = logger::run_log_errors([&] {
                std::shared_ptr<parallel::ordered_queue> part_q = std::make_shared<parallel::ordered_queue>();
                const auto submit_batch = [&](const size_t bi) {
                    sched.submit(task_id, -static_cast<int64_t>(bi), [&, bi] {
                        try {
                            if (!part_c.cancel()) {
                                {
                                    const auto finalize_after_batch = bi + 1 == batches.size()
                                        || _cr.make_slot(batches[bi + 1].front()->first_slot).epoch() != _cr.make_slot(batches[bi].front()->first_slot).epoch();
                                    auto stats = _process_batch_stage1(
                                        _cr, bi, batches[bi], _cfg, finalize_after_batch, batch_handoff[bi]);
                                    mutex::scoped_lock lk { all_mutex };
                                    all += stats;
                                }
                                part_q->put(bi);
                                part_q->take_all();
                                part_c.try_push(part_q->next());
                            }
                        } catch (const std::exception &ex) {
                            stop_submitting.store(true, std::memory_order_release);
                            // cancel all scheduled parse tasks for batches after this one
                            // since now we know that the work cannot be incorporated
                            sched.cancel([bi](const auto &t_task_id, const auto &t_param) {
                                return task_id == t_task_id && t_param && std::any_cast<size_t>(*t_param) >= bi;
                            });
                            std::vector<std::string> chunk_info {};
                            for (const auto *c_ptr: batches[bi])
                                chunk_info.emplace_back(fmt::format("{} first slot: {}", c_ptr->rel_path(), c_ptr->first_slot));
                            const auto msg = fmt::format("batch {}: pre-processing failed: {} chunks: {}", bi, ex.what(), chunk_info);
                            throw error(msg);
                        }
                    }, bi);
                };
                const auto submit_window = [&](const size_t num_consumed) {
                    const auto target = std::min(batches.size(), num_consumed + max_run_ahead);
                    for (;;) {
                        if (stop_submitting.load(std::memory_order_acquire))
                            return;
                        auto bi = next_batch_to_submit.load(std::memory_order_relaxed);
                        if (bi >= target)
                            return;
                        if (next_batch_to_submit.compare_exchange_weak(
                                bi, bi + 1, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                            submit_batch(bi);
                        }
                    }
                };
                replenish_stage1 = [&](const size_t num_consumed) {
                    submit_window(num_consumed);
                };
                logger::debug(
                    "txwit: bounded stage-1 run-ahead batches: {} worker_multiplier: {} workers: {} tx_frame_zstd_level: {}",
                    max_run_ahead, run_ahead_worker_multiplier, sched.num_workers(), tx_frame_zstd_level);
                submit_window(0);
                // let the running consumer tasks to finish
                sched.process(true);
                logger::debug("txwit: exited the processs_batches loop part_q: {} part_c: {}", part_q->next(), part_c.next());
                part_q->take_all();
                progress::get().update_inform("txwit", part_c.next(), batches.size());
                logger::debug("txwit: pushed the remaining parts part_q: {} part_c: {}", part_q->next(), part_c.next());
                if (part_c.next() < part_q->next()) {
                    if (!part_c.try_push(part_q->next())) [[unlikely]]
                        throw error("failed to schedule the final work items");
                    sched.process(true);
                }
                logger::debug("txwit: consumed the remaining parts part_q: {} part_c: {}", part_q->next(), part_c.next());
            });
            // ensure there are no runaway tasks
            if (ex_ptr)
                sched.process(true);
            return all;
        }
    };

    optional_point validate(const chunk_registry &cr, const optional_point &intersection, const optional_point &to, const witness_type typ,
        const error_handler_func &error_handler)
    {
        validator v { cr, intersection, to, typ, error_handler };
        return v.validate();
    }
}
