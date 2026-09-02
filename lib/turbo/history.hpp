#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <string>
#include <turbo/cardano/common/mocks.hpp>
#include <turbo/common/scheduler.hpp>
#include <turbo/chunk-registry.hpp>
#include <turbo/index/pay-ref.hpp>
#include <turbo/index/stake-ref.hpp>
#include <turbo/index/tx.hpp>
#include <turbo/index/txo-use.hpp>
#include <turbo/indexer.hpp>
#include <turbo/json.hpp>
#include <turbo/util.hpp>
#include <utility>

namespace turbo {
    struct transaction_output {
        uint64_t use_offset = 0;
        uint16_t out_idx = 0;
        cardano::tx_size use_size {};
        cardano::amount amount {};
        cardano::multi_balance assets {};
    };

    struct transaction_input {
        uint64_t tx_offset = 0;
        cardano::amount amount {};
        cardano::multi_balance assets {};
    };

    struct transaction {
        std::vector<transaction_output> outputs {};
        std::vector<transaction_input> inputs {};
        cardano::tx_size size {};
        cardano::tx_hash hash {};
        uint64_t slot = 0;

        bool operator<(const auto &b) const
        {
            return slot < b.slot;
        }

        cardano::multi_balance_change balance_change() const
        {
            cardano::multi_balance_change changes {};
            for (const auto &out: outputs) {
                changes["ADA"] += out.amount;
                for (const auto &[asset_name, amount]: out.assets) {
                    changes[asset_name] += amount;
                }
            }
            for (const auto &in: inputs) {
                changes["ADA"] -= (int64_t)in.amount;
                for (const auto &[asset_name, amount]: in.assets) {
                    changes[asset_name] -= amount;
                }
            }
            return changes;
        }

        template<typename IT>
        IT to_string(IT it, const cardano::config &cfg) const
        {
            return fmt::format_to(it, "slot: {} tx: {} change: {}\n", cardano::slot { slot, cfg }, hash, balance_change());
        }

        json::object to_json(const cardano::tail_relative_stake_map &tail_relative_stake, const cardano::config &cfg) const
        {
            json::object j {
                { "hash", fmt::format("{}", hash) },
                { "slot", cardano::slot { slot, cfg }.to_json() },
                { "balanceChange", fmt::format("{}", balance_change()) },
                { "spentInputs", inputs.size() },
                { "newOutputs", outputs.size() },
                { "relativeStake", cardano::tx_base::slot_relative_stake(tail_relative_stake, slot) }
            };
            return j;
        }
    };

    struct transaction_map: std::map<uint64_t, transaction> {
        using std::map<uint64_t, transaction>::map;

        inline json::array to_json(const cardano::tail_relative_stake_map &tail_relative_stake, const cardano::config &cfg,
                                   size_t offset=0, size_t max_items=1000) const
        {
            size_t end_offset = offset + max_items;
            if (end_offset > size())
                end_offset = size();
            json::array txs {};
            // transactions are returned in descending order!
            size_t i = 0;
            for (const auto &[cr_offset, tx]: *this | std::views::reverse) {
                if (i >= offset)
                    txs.emplace_back(tx.to_json(tail_relative_stake, cfg));
                if (++i >= end_offset)
                    break;
            }
            return txs;
        }
    };

    struct reconstructor;

    template<typename T>
    struct history {
        const cardano::config &cfg;
        T id {};
        transaction_map transactions {};
        uint64_t last_slot = 0;
        uint64_t num_disk_reads = 0;
        uint64_t num_idx_reads = 0;
        uint64_t total_tx_outputs = 0;
        uint64_t total_tx_outputs_unspent = 0;
        uint64_t total_utxo_balance = 0;
        cardano::multi_balance balance_assets {};
        uint64_t total_withdrawals = 0;
        bool full_history = false;

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.id, self.transactions, self.last_slot, self.num_disk_reads, self.num_idx_reads,
                self.total_tx_outputs, self.total_tx_outputs_unspent, self.total_utxo_balance, self.balance_assets,
                self.total_withdrawals, self.full_history);
        }

        template<typename IDX>
        bool find_incoming_txos(index::reader_multi<IDX> &ref_idx)
        {
            timer t { "find referenced tx outputs", logger::level::debug };
            IDX search_item { id };
            auto [ ref_count, ref_item ] = ref_idx.find(search_item);
            for (size_t i = 0; i < ref_count; i++) {
                auto it = transactions.emplace_hint(transactions.end(), static_cast<uint64_t>(ref_item.offset), transaction { .size=ref_item.size });
                it->second.outputs.emplace_back(0, static_cast<uint16_t>(ref_item.out_idx));
                if (i < ref_count - 1)
                    ref_idx.read(ref_item);
            }
            return !transactions.empty();
        }

        std::vector<uint64_t> find_used_txos(scheduler &sched, const index::reader_multi_mt<index::txo_use::item> &txo_use_idx)
        {
            timer t { "identify used tx outputs", logger::level::debug };
            auto &p = progress::get();
            size_t num_txos = 0;
            std::vector<uint64_t> txo_tasks {};
            txo_tasks.reserve(transactions.size());
            for (const auto &it: transactions) {
                txo_tasks.emplace_back(it.first);
                num_txos += it.second.outputs.size();
            }
            if (transactions.size() < 100000)
                _find_used_txos_small(p, sched, txo_tasks, txo_use_idx);
            else
                _find_used_txos_large(p, sched, num_txos, txo_use_idx);
            p.done("find spent txos");
            p.inform();
            return txo_tasks;
        }

        void add_spending_txs(std::vector<uint64_t> &txo_tasks)
        {
            timer t { "add spending transactions", logger::level::debug };
            // use txo_tasks because txos map is being updated in this loop
            for (auto tx_off: txo_tasks) {
                auto &txo_item = transactions.at(tx_off);
                for (const auto &out: txo_item.outputs) {
                    if (out.use_offset != 0) {
                        auto it = transactions.emplace_hint(transactions.end(), out.use_offset, transaction { .size=out.use_size });
                        it->second.inputs.emplace_back(tx_off, out.amount, out.assets);
                    }
                }
            }
        }

        inline void fill_raw_tx_data(scheduler &sched, chunk_registry &cr, const reconstructor &r, const bool spending_only=false);

        void compute_balances()
        {
            timer t { "compute account balance", logger::level::debug };
            for (const auto &[offset, tx]: transactions) {
                for (const auto &out: tx.outputs) {
                    ++total_tx_outputs;
                    if (out.use_offset != 0)
                        continue;
                    ++total_tx_outputs_unspent;
                    total_utxo_balance += out.amount;
                    for (const auto &[asset_name, amount]: out.assets) {
                        balance_assets[asset_name] += amount;
                    }
                }
            }
        }

        cardano::amount utxo_balance() const
        {
            return cardano::amount { total_utxo_balance };
        }

        uint64_t reward_withdrawals() const
        {
            return total_withdrawals;
        }

        json::object to_json(const cardano::tail_relative_stake_map &tail_relative_stake, const cardano::config &cfg, const size_t max_items=1000) const
        {
            return json::object {
                { "id", id.to_json() },
                { "txCount", transactions.size() },
                { "balance", fmt::format("{}", cardano::amount { total_utxo_balance }) },
                { "assetCount", balance_assets.size() },
                { "assets", balance_assets.to_json(0, max_items) },
                { "withdrawals", total_withdrawals },
                { "transactions", transactions.to_json(tail_relative_stake, cfg, 0, max_items) }
            };
        }

    private:
        struct parse_task {
            chunk_registry::chunk_info chunk {};
            std::vector<typename transaction_map::value_type *> tasks {};
        };
        using parse_tasks = std::map<uint64_t, parse_task>;

        void _find_used_txos_small(progress &p, scheduler &sched, std::vector<uint64_t> &txo_tasks, const index::reader_multi_mt<index::txo_use::item> &txo_use_idx)
        {
            const size_t num_parts = sched.num_workers();
            std::atomic_size_t num_ready { 0 };
            std::atomic_size_t new_reads { 0 };
            for (size_t i = 0; i < num_parts; ++i) {
                const size_t start_idx = i * txo_tasks.size() / num_parts;
                const size_t end_idx = (i + 1) * txo_tasks.size() / num_parts;
                sched.submit("search-txo-use", 100, [this, start_idx, end_idx, &txo_tasks, &txo_use_idx, &p, &num_ready, &new_reads]() {
                    auto txo_use_data = txo_use_idx.init_thread();
                    index::txo_use::item search_item {};
                    for (size_t j = start_idx; j < end_idx; ++j) {
                        auto &txo_item = transactions.at(txo_tasks[j]);
                        search_item.hash = txo_item.hash;
                        for (auto &out: txo_item.outputs) {
                            search_item.out_idx = out.out_idx;
                            const auto [ use_count, use_item ] = txo_use_idx.find(search_item, txo_use_data);
                            if (use_count > 0) {
                                if (use_count > 1) [[unlikely]]
                                    throw error(fmt::format("internal error: multiple txo-use entries for the same tx offset {}!", txo_tasks[j]));
                                out.use_offset = use_item.offset;
                                out.use_size = use_item.size;
                            }
                        }
                        if (num_ready.fetch_add(1) % 1000 == 0) {
                            p.update("find spent txos", num_ready, transactions.size());
                        }
                    }
                    new_reads.fetch_add(txo_use_data.num_reads, std::memory_order_relaxed);
                });
            }
            sched.process(true);
            const auto new_reads_val = new_reads.load(std::memory_order_acquire);
            num_disk_reads += new_reads_val;
            num_idx_reads += new_reads_val;
        }

        void _find_used_txos_large(progress &p, scheduler &sched, size_t num_txos, const index::reader_multi_mt<index::txo_use::item> &txo_use_idx)
        {
            std::atomic_size_t num_ready { 0 };
            std::atomic_size_t new_reads { 0 };
            const size_t num_parts = txo_use_idx.num_parts();
            for (size_t pi = 0; pi < num_parts; ++pi) {
                sched.submit("search-txo-use", 100, [this, pi, num_txos, &txo_use_idx, &p, &num_ready, &new_reads]() {
                    auto txo_use_data = txo_use_idx.init_thread(pi);
                    index::txo_use::item item {};
                    while (txo_use_idx.read_part(pi, item, txo_use_data)) {
                        auto it = transactions.find(item.offset);
                        if (it == transactions.end())
                            continue;
                        auto &txo_item = it->second;
                        for (auto &out: txo_item.outputs) {
                            if (item.out_idx != out.out_idx)
                                continue;
                            out.use_offset = item.offset;
                            out.use_size = item.size;
                            if (num_ready.fetch_add(1) % 1000 == 0) {
                                p.update("find spent txos", num_ready, num_txos);
                            }
                        }
                    }
                    new_reads.fetch_add(txo_use_data.num_reads, std::memory_order_relaxed);
                    return txo_use_data.num_reads;
                });
            }
            sched.process(true);
            const auto new_reads_val = new_reads.load(std::memory_order_acquire);
            num_disk_reads += new_reads_val;
            num_idx_reads += new_reads_val;
        }
    };

    struct reconstructor {
        using find_tx_res = cardano::tx_container;

        reconstructor(chunk_registry &cr):
            _cr { cr },
            _stake_ref_idx { _cr.indexer().reader_paths("stake-ref") },
            _pay_ref_idx { _cr.indexer().reader_paths("pay-ref") },
            _tx_idx { _cr.indexer().reader_paths("tx") },
            _txo_use_idx { _cr.indexer().reader_paths("txo-use") }
        {
        }

        uint64_t last_slot() const
        {
            return _cr.max_slot();
        }

        std::optional<find_tx_res> find_tx(const buffer &tx_hash)
        {
            std::optional<find_tx_res> res {};
            if (const auto [tx_count, tx_item] = _tx_idx.find(index::tx::item { tx_hash }); tx_count > 0) {
                res.emplace(_cr.find_block_by_offset(tx_item.offset), tx_item.offset, _cr.read(tx_item.offset).get(), 0, _cr.config());
            }
            return res;
        }

        template<typename T>
        history<T> find_history(const T &id)
        {
            if constexpr (std::is_same_v<T, cardano::stake_ident>)
                return _history(_stake_ref_idx, id);
            if constexpr (std::is_same_v<T, cardano::pay_ident>)
               return _history(_pay_ref_idx, id);;
            throw error(fmt::format("unsupported type for find_history: {}", typeid(T).name()));
        }

        storage::block_info find_block(uint64_t tx_offset) const
        {
            return _cr.find_block_by_offset(tx_offset);
        }
    private:
        chunk_registry &_cr;
        index::reader_multi<index::stake_ref::item> _stake_ref_idx;
        index::reader_multi<index::pay_ref::item> _pay_ref_idx;
        index::reader_multi<index::tx::item> _tx_idx;
        index::reader_multi_mt<index::txo_use::item> _txo_use_idx;

        template<typename IDX, typename ID>
        const history<ID> _history(index::reader_multi<IDX> &ref_idx, const ID &id)
        {
            timer t { "history reconstruction", logger::level::debug };
            progress_guard pg { "fetch incoming txos", "find spent txos", "load spent txo data" };
            history<ID> hist { _cr.config(), id };
            if (_cr.empty())
                return hist;
            hist.last_slot = _cr.max_slot();
            if (!hist.find_incoming_txos(ref_idx))
                return hist;
            hist.fill_raw_tx_data(_cr.sched(), _cr, *this);
            {
                auto txo_tasks = hist.find_used_txos(_cr.sched(), _txo_use_idx);
                hist.add_spending_txs(txo_tasks);
            }
            hist.fill_raw_tx_data(_cr.sched(), _cr, *this, true);
            hist.compute_balances();
            hist.full_history = true;
            return hist;
        }
    };

    template<typename T>
    void history<T>::fill_raw_tx_data(scheduler &sched, chunk_registry &cr, const reconstructor &r, const bool spending_only)
    {
        const timer t1 { "fill_raw_tx_data - full", logger::level::debug };
        const std::string progress_id { spending_only ? "load spent txo data" : "fetch incoming txos" };
        // group txos by their chunk based on their offsets
        parse_tasks chunk_tasks {};
        for (auto &tx_it: transactions) {
            if (spending_only && !tx_it.second.outputs.empty())
                continue;
            const auto &chunk = cr.find_offset(tx_it.first);
            const auto [task_it, created] = chunk_tasks.emplace(chunk.offset, parse_task { chunk });
            task_it->second.tasks.emplace_back(&tx_it);
        }

        const timer t2 { fmt::format("fill_raw_tx_data - {} load and parse tasks", chunk_tasks.size()), logger::level::debug };
        // extract transaction data
        auto &p = progress::get();
        if (!chunk_tasks.empty()) {
            std::atomic_size_t num_ready = 0;
            for (auto &[chunk_offset, chunk_info]: chunk_tasks) {
                sched.submit("parse-chunk", 100, [&] {
                    const auto data = zstd::read(cr.full_path(chunk_info.chunk.rel_path()));
                    const buffer buf { data };
                    for (auto tx_ptr: chunk_info.tasks) {
                        auto &[tx_offset, tx_item] = *tx_ptr;
                        if (tx_offset < chunk_offset) [[unlikely]]
                            throw error(fmt::format("task offset: {} < chunk_offset: {}!", tx_offset, chunk_offset));
                        auto tx_size = static_cast<size_t>(tx_item.size);
                        const size_t tx_chunk_offset = tx_offset - chunk_offset;
                        // tx_size is imprecise so bound it down to the chunk size
                        if (tx_size > buf.size() - tx_chunk_offset)
                            tx_size = buf.size() - tx_chunk_offset;
                        const auto tx_buf = buf.subbuf(tx_chunk_offset, tx_size);
                        try {
                            auto tx_raw = cbor::zero2::parse(tx_buf);
                            const auto block_meta = r.find_block(tx_offset);
                            const auto tx = cardano::tx_container(block_meta, tx_offset, tx_raw.get(), 0, cr.config());
                            tx_item.hash = tx->hash();
                            tx_item.slot = cr.make_slot(block_meta.slot);
                            for (auto &txo_req: tx_item.outputs) {
                                const auto &txo = tx->outputs().at(txo_req.out_idx);
                                txo_req.amount = txo.coin;
                                for (const auto &[policy_id, policy_assets]: txo.assets) {
                                    for (const auto &[asset_name, coin]: policy_assets) {
                                        txo_req.assets[asset_name.to_string(policy_id)] = coin;
                                    }
                                }
                            }
                        } catch (const std::exception &ex) {
                            throw error(fmt::format("cannot parse tx at offset {} size {}: {}", tx_offset, static_cast<size_t>(tx_item.size), ex.what()));
                        }
                    }
                    const auto new_num_ready = num_ready.fetch_add(1, std::memory_order_relaxed) + 1;
                    p.update(progress_id, new_num_ready, chunk_tasks.size());
                });
            }
            sched.process(true);
            num_disk_reads += chunk_tasks.size();
        }
        p.done(progress_id);
        p.inform();
    }

}

namespace fmt {
    template<>
    struct formatter<turbo::transaction>: formatter<size_t> {
        template<typename FormatContext>
        auto format(const auto &tx, FormatContext &ctx) const -> decltype(ctx.out()) {
            return tx.to_string(ctx.out(), turbo::cardano::config::get());
        }
    };

    template<typename T>
    struct formatter<turbo::history<T>>: formatter<size_t> {
        template<typename FormatContext>
        auto format(const auto &h, FormatContext &ctx) const -> decltype(ctx.out()) {
            auto out_it = ctx.out();
            if (h.full_history) {
                for (const auto &[offset, tx]: h.transactions)
                   out_it = tx.to_string(out_it, h.cfg);
            }
            out_it = fmt::format_to(out_it, "transaction outputs affecting {}: {} of them unspent: {}\n",
                h.id, h.total_tx_outputs, h.total_tx_outputs_unspent);
            out_it = fmt::format_to(out_it, "available balance without rewards: {}\n", turbo::cardano::amount { h.utxo_balance() });
            if (h.balance_assets.size() > 0)
                out_it = fmt::format_to(out_it, "asset balances: {}\n", h.balance_assets);
            return fmt::format_to(out_it, "last indexed slot: {}, # random reads: {} of them from indices: {} ({:0.1f}%)\n",
                h.last_slot, h.num_disk_reads, h.num_idx_reads,
                h.num_disk_reads > 0 ? 100 * (double)h.num_idx_reads / h.num_disk_reads: 0.0);
        }
    };
}
