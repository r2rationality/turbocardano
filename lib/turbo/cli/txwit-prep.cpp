/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cardano/ledger/state.hpp>
#include <turbo/chunk-registry.hpp>
#include <turbo/index/merge-zpp.hpp>
#include <turbo/index/utxo.hpp>
#include <turbo/plutus/context.hpp>
#include <turbo/storage/partition.hpp>
#include <turbo/zpp-stream.hpp>
#include "common.hpp"

namespace turbo::cli::txwit_prep {
    using namespace cardano;
    using namespace cardano::ledger;
    using namespace plutus;

    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "txwit-prep";
            cmd.desc = "Validate simple tx witnesses and prepare context files for the validation of script witnesses";
            cmd.args.expect({ "<data-dir>", "<output-dir>" });
        }

        void run(const arguments &args) const override
        {
            file::set_max_open_files();
            chunk_registry cr { args.at(0), chunk_registry::mode::store };
            const auto &out_dir = args.at(1);
            std::filesystem::create_directories(out_dir);

            mutex::unique_lock::mutex_type done_mutex alignas(mutex::alignment) {};
            uint64_t next_epoch = 0;
            std::set<uint64_t> ready_epochs {};
            std::vector<param_update> updates {};
            utxo_processor proc { cr, out_dir };

            storage::parse_parallel_epoch<part_info>(cr,
                [&](auto &part, const auto &blk) {
                    const auto &block_info = cr.find_block_by_offset(blk.offset());
                    blk->foreach_update_proposal([&](const auto &prop) {
                        part.updates.emplace_back(blk->slot_object(), prop);
                    });
                    blk->foreach_update_vote([&](const auto &vote) {
                        part.updates.emplace_back(blk->slot_object(), vote);
                    });
                    blk->foreach_tx([&](const auto &tx) {
                        size_t num_redeemers = 0;
                        tx.foreach_redeemer([&](const auto &) {
                            ++num_redeemers;
                        });
                        if (num_redeemers) {
                            stored_tx_context ctx { tx.hash(), num_redeemers, uint8_vector { tx.raw() },
                                uint8_vector { tx.witness_raw() }, block_info };
                            bool complete = true;
                            tx.foreach_input([&](const auto &txi) {
                                auto &txo = ctx.inputs.emplace_back(txi);
                                if (const auto txo_it = part.utxos.find(txo.id); txo_it != part.utxos.end())
                                    txo.data = txo_it->second;
                                else
                                    complete = false;
                            });
                            tx.foreach_referenced_input([&](const auto &txi) {
                                auto &txo = ctx.ref_inputs.emplace_back(txi);
                                if (const auto txo_it = part.utxos.find(txo.id); txo_it != part.utxos.end())
                                    txo.data = txo_it->second;
                                else
                                    complete = false;
                            });
                            if (complete) {
                                part.tx_res_s.write(ctx, num_redeemers);
                                ++part.num_complete_txs;
                            } else {
                                part.tx_unres_s.write(ctx);
                                ++part.num_incomplete_txs;
                            }
                        }
                        size_t out_idx = 0;
                        tx.foreach_output([&](const auto &tx_out) {
                            ++part.num_outputs;
                            _add_utxo(part.utxos, tx, tx_out, out_idx++);
                        });
                        tx.foreach_input([&](const auto &txi) {
                            ++part.num_inputs;
                            _del_utxo(part.utxos, txi);
                        });
                    });
                    blk->foreach_invalid_tx([&](const auto &tx) {
                        // UTXOs used as collaterals are processed in validator.cpp:_apply_ledger_state_updates_for_epoch
                        if (const auto *babbage_tx = dynamic_cast<const cardano::babbage::tx *>(&tx); babbage_tx) {
                            if (const auto c_ret = babbage_tx->collateral_return(); c_ret) {
                                const auto txo_idx = babbage_tx->outputs().size();
                                logger::debug("slot: {} found collateral refund {}#{}: {}", tx.block().slot(), tx.hash(), txo_idx, *c_ret);
                                ++part.num_outputs;
                                _add_utxo(part.utxos, tx, *c_ret, txo_idx);
                            }
                        }
                    });
                },
                [&](const size_t epoch_no, const storage::partition &) {
                    return part_info {
                        chunked_zpp_stream { fmt::format("{}/ctx/{}-1p", out_dir, epoch_no) },
                        zpp_stream::write_stream(fmt::format("{}/tx-unres/{}.zpp", out_dir, epoch_no)),
                        timer { fmt::format("prep-epoch-{}", epoch_no), logger::level::info }
                    };
                },
                [&](auto &&part_o, const size_t epoch_no, const auto &) {
                    {
                        // move to a local var to ensure its destructor is executed before tasks are scheduled
                        part_info part { std::move(part_o) };
                        for (size_t pi = 0; pi < part.utxos.num_parts; ++pi)
                            zpp::save_zstd(fmt::format("{}/utxo/{}-{:02X}.zpp", out_dir, epoch_no, pi), part.utxos.partition(pi));
                        mutex::scoped_lock lk { done_mutex };
                        ready_epochs.emplace(epoch_no);
                        for (auto it = ready_epochs.begin(); it != ready_epochs.end() && next_epoch == *it; it = ready_epochs.erase(it)) {
                            next_epoch = *it + 1;
                        }
                        updates.reserve(updates.size() + part.updates.size());
                        for (auto &&u: part.updates)
                            updates.emplace_back(std::move(u));
                        logger::info("epoch {} took {:.3f} secs utxos: {} outputs: {} inputs: {} num_complete_txs: {} num_incomplete_txs: {}",
                            epoch_no, part.t.stop(false),
                            part.utxos.size(), part.num_outputs, part.num_inputs, part.num_complete_txs, part.num_incomplete_txs);
                    }
                    proc.schedule_apply(next_epoch);
                }
            );
            proc.schedule_apply(next_epoch);
            cr.sched().process(true);
            _write_cost_models(out_dir, updates, next_epoch);
            std::filesystem::remove_all(fmt::format("{}/utxo", out_dir));
            std::filesystem::remove_all(fmt::format("{}/tx-unres", out_dir));
        }
    private:
        struct param_update {
            cardano::slot slot;
            std::variant<param_update_proposal, param_update_vote> update {};

            bool operator<(const param_update &o) const
            {
                return slot < o.slot;
            }
        };
        using param_update_list = std::vector<param_update>;

        struct cost_model_update {
            uint64_t epoch;
            plutus_cost_models models;
        };
        using cost_model_update_list = std::vector<cost_model_update>;

        struct chunked_zpp_stream {
            chunked_zpp_stream(const std::string &prefix, const size_t limit=2000):
                _prefix { prefix }, _limit { limit },
                _stream { std::make_unique<zpp_stream::write_stream>(_chunk_path(_part_idx)) }
            {
                if (!_limit) [[unlikely]]
                    throw error("limit must be non-zero!");
            }

            template<typename T>
            void write(const T &v, const size_t weight)
            {
                _stream->write(v);
                _part_cost += weight;
                if (_part_cost >= _limit) {
                    _stream = std::make_unique<zpp_stream::write_stream>(_chunk_path(++_part_idx));
                    _part_cost = 0;
                }
            }
        private:
            const std::string _prefix;
            const size_t _limit;
            size_t _part_idx = 0;
            size_t _part_cost = 0;
            std::unique_ptr<zpp_stream::write_stream> _stream;

            std::string _chunk_path(const size_t chunk_idx) const
            {
                return fmt::format("{}-{}.zpp", _prefix, chunk_idx);
            }
        };

        struct part_info {
            chunked_zpp_stream tx_res_s;
            zpp_stream::write_stream tx_unres_s;
            timer t;
            txo_map utxos {};
            param_update_list updates {};
            size_t num_outputs = 0;
            size_t num_inputs = 0;
            size_t num_complete_txs = 0;
            size_t num_incomplete_txs = 0;
        };

        struct utxo_processor {
            utxo_processor(const chunk_registry &cr, const std::string &dir): _cr { cr }, _dir { dir }
            {
            }

            void schedule_apply(const uint64_t end_epoch)
            {
                if (end_epoch > _next_epoch.load(std::memory_order_relaxed) && !_running.load(std::memory_order_relaxed)) {
                    bool exp = false;
                    if (_running.compare_exchange_strong(exp, true, std::memory_order_acquire, std::memory_order_relaxed)) {
                        const auto start_epoch = _next_epoch.load(std::memory_order_acquire);
                        _cr.sched().submit("utxo-apply", 1000, [&, start_epoch, end_epoch] {
                            logger::run_log_errors([&] {
                                for (auto epoch = start_epoch; epoch < end_epoch; ++epoch) {
                                    _apply_epoch(epoch);
                                }
                            }, [&] {
                                _running.store(false, std::memory_order_release);
                            });
                        });
                    }
                }
            }
        private:
            const chunk_registry &_cr;
            const std::string _dir;
            std::atomic_uint64_t _next_epoch { 0 };
            std::atomic_bool _running { false };
            mutex::unique_lock::mutex_type _m alignas(mutex::alignment) {};
            txo_map _utxos { _cr.config().byron_utxos };

            void _apply_epoch(const uint64_t epoch)
            {
                auto exp = epoch;
                if (!_next_epoch.compare_exchange_strong(exp, epoch + 1, std::memory_order_acq_rel, std::memory_order_relaxed)) [[unlikely]]
                    throw error(fmt::format("failed to progress utxo epoch from: {} to {}", epoch, epoch + 1));
                timer t { fmt::format("apply epoch {}", epoch), logger::level::info };
                txo_map e_utxos {};

                auto &sched = _cr.sched();
                std::vector<stored_tx_context> unres_ctxs {};
                unres_ctxs.reserve(1 << 17);
                const auto unres_path = fmt::format("{}/tx-unres/{}.zpp", _dir, epoch);
                {
                    timer t1 { fmt::format("load-unres-ctx-{}", epoch), logger::level::info };
                    zpp_stream::read_stream tx_unres_s { unres_path };
                    while (!tx_unres_s.eof()) {
                        unres_ctxs.emplace_back(tx_unres_s.read<stored_tx_context>());
                    }
                }
                std::filesystem::remove(unres_path);
                timer t2 { fmt::format("resolve-unres-ctx-{}", epoch), logger::level::info };
                static const std::string task2_id { "ref-epoch-scripts" };
                static constexpr size_t deref_workers = 256;
                const auto part_size = (unres_ctxs.size() + deref_workers - 1) / deref_workers;
                sched.wait_all(task2_id, [&](const auto &, const auto &submit_f) {
                    for (size_t pi = 0; pi < deref_workers; ++pi ) {
                        submit_f({ 2000, task2_id, [&, pi] {
                            const auto part_begin = part_size * pi;
                            const auto part_end = std::min(unres_ctxs.size(), part_size * (pi + 1));
                            chunked_zpp_stream tx_res_s { fmt::format("{}/ctx/{}-2p-{:02X}", _dir, epoch, pi) };
                            for (size_t i = part_begin; i < part_end; ++i) {
                                auto &ctx = unres_ctxs[i];
                                for (auto &txi: ctx.inputs) {
                                    if (txi.data.empty()) {
                                        const auto txo_it = _utxos.find(txi.id);
                                        if (txo_it == _utxos.end()) [[unlikely]]
                                            throw error(fmt::format("failed to resolve TXO {}", txi.id));
                                        txi.data = txo_it->second;
                                    }
                                }
                                for (auto &txi: ctx.ref_inputs) {
                                    if (txi.data.empty()) {
                                        const auto txo_it = _utxos.find(txi.id);
                                        if (txo_it == _utxos.end()) [[unlikely]]
                                            throw error(fmt::format("failed to resolve TXO {}", txi.id));
                                        txi.data = txo_it->second;
                                    }
                                }
                                tx_res_s.write(ctx, ctx.num_redeemers);
                            }
                        }});
                    }
                });
                t2.stop_and_print();
                timer t4 {  fmt::format("apply-utxos-{}", epoch), logger::level::info };
                static const std::string task_id { "load-epoch-utxo" };
                sched.wait_all(task_id, [&](const scheduler::todo_count_t &, const scheduler::submit_func_t &submit_f) {
                    for (size_t pi = 0; pi < e_utxos.num_parts; ++pi) {
                        submit_f({ 2000, task_id, [&, pi] {
                            auto &utxo_part = _utxos.partition(pi);
                            auto &ue_part = e_utxos.partition(pi);
                            const auto part_path = fmt::format("{}/utxo/{}-{:02X}.zpp", _dir, epoch, pi);
                            zpp::load_zstd(ue_part, part_path);
                            for (auto &&[txo_id, txo_data]: ue_part) {
                                if (!txo_data.address_raw.empty()) {
                                    if (!txo_data.empty()) [[likely]] {
                                        if (auto [it, created] = utxo_part.try_emplace(txo_id, std::move(txo_data)); !created) [[unlikely]]
                                            logger::warn("a non-unique TXO {}!", it->first);
                                    }
                                } else {
                                    if (auto it = utxo_part.find(txo_id); it != utxo_part.end()) [[likely]] {
                                        utxo_part.erase(it);
                                    } else {
                                        throw error(fmt::format("epoch: {} part: {:02X} request to remove an unknown TXO {}!", epoch, pi, txo_id));
                                    }
                                }
                            }
                            std::filesystem::remove(part_path);
                        }});
                    }
                });
            }
        };

        static void _write_cost_models(const std::string &out_dir, param_update_list &updates, const uint64_t end_epoch)
        {
            std::sort(updates.begin(), updates.end());
            ledger::state st {};
            st.start_epoch(0);
            cost_model_update_list epoch_cost_models {};
            epoch_cost_models.emplace_back(0, st.params().plutus_cost_models);
            auto updates_it = updates.begin();
            while (st.epoch() < end_epoch) {
                if (st.params().plutus_cost_models != epoch_cost_models.back().models) {
                    logger::info("cost model update at epoch: {}", st.epoch());
                    epoch_cost_models.emplace_back(st.epoch(), st.params().plutus_cost_models);
                }
                for (; updates_it != updates.end() && updates_it->slot.epoch() == st.epoch(); ++updates_it) {
                    std::visit([&](const auto &u) {
                        using T = std::decay_t<decltype(u)>;
                        if constexpr (std::is_same_v<T, param_update_proposal>) {
                            st.propose_update(updates_it->slot, u);
                            //std::cout << fmt::format("update proposal slot: {} data: {}\n", e.slot, std::get<cardano::param_update_proposal>(e.update));
                        } else if constexpr (std::is_same_v<T, param_update_vote>) {
                            //std::cout << fmt::format("update vote slot: {} data: {}\n", e.slot, std::get<cardano::param_update_vote>(e.update));
                            st.proposal_vote(updates_it->slot, u);
                        } else {
                            throw error(fmt::format("unsupported parameter update type: {}", typeid(T).name()));
                        }
                    }, updates_it->update);
                }
                st.start_epoch(st.epoch() + 1);
            }
            zpp_stream::write_stream ws { fmt::format("{}/cost-models/all.zpp", out_dir) };
            ws.write(epoch_cost_models);
        }

        static void _del_utxo(txo_map &idx, const tx_out_ref &txo_id)
        {
            auto [it, created] = idx.try_emplace(txo_id);
            // If a txo is created and consumed within the same chunk, don't report it.
            if (!created) [[unlikely]] {
                if (!it->second.address_raw.empty()) [[likely]] {
                    idx.erase(it);
                } else {
                    throw error(fmt::format("found a non-unique TXO in the same chunk {}", txo_id));
                }
            }
        }

        static void _add_utxo(txo_map &idx, const cardano::tx_base &tx, const tx_output &txo, const size_t txo_idx)
        {
            if (const auto [it, created] = idx.try_emplace(tx_out_ref { tx.hash(), txo_idx }, txo); !created) [[unlikely]]
                throw error(fmt::format("found a non-unique TXO {}#{}", tx.hash(), txo_idx));
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}
