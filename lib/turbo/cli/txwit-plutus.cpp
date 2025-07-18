/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/chunk-registry.hpp>
#include <turbo/history.hpp>
#include <turbo/plutus/context.hpp>
#include <turbo/plutus/costs.hpp>
#include <turbo/zpp-stream.hpp>
#include "common.hpp"

namespace turbo::cli::txwit_plutus {
    using namespace cardano;
    using namespace plutus;

    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "txwit-plutus";
            cmd.desc = "validate plutus witnesses using the context files prepared with txwit-prep";
            cmd.args.expect({ "<context-dir>" });
            cmd.opts.try_emplace("epoch", "evaluate only the given epoch");
            cmd.opts.try_emplace("file", "evaluate only the given file");
            cmd.opts.try_emplace("tx", "evaluate only the given transaction");
            cmd.opts.try_emplace("workers", "force the scheduler to use a given number of wokers");
        }

        void run(const arguments &args, const options &opts) const override
        {
            const auto &ctx_dir = args.at(0);
            user_config cfg { opts };
            scheduler sched { cfg.num_workers };
            parsed_models_update_list epoch_cost_models {};
            {
                zpp_stream::read_stream s { fmt::format("{}/cost-models/all.zpp", ctx_dir) };
                const auto updates  = s.read<cost_model_update_list>();
                for (const auto &u: updates) {
                    epoch_cost_models.emplace_back(u.epoch, costs::parse(u.models));
                }
            }
            file::path_list paths {};
            for (auto &&p: file::files_with_ext_path(fmt::format("{}/ctx", ctx_dir), ".zpp")) {
                if (cfg.epoch) {
                    const auto stem = p.stem().string();
                    if (const auto pos = stem.find('-'); pos == std::string::npos || std::stoull(stem.substr(0, pos)) != cfg.epoch)
                        continue;
                }
                paths.emplace_back(std::move(p));
            }
            std::sort(paths.begin(), paths.end());
            mutex::unique_lock::mutex_type wits_mutex alignas(mutex::alignment) {};
            wit_cnt wits {};
            std::atomic_size_t ok = 0;
            std::atomic_size_t err = 0;
            for (size_t i = 0; i < paths.size(); ++i) {
                if (std::filesystem::file_size(paths[i]) > 0 && (!cfg.file || *cfg.file == paths[i].filename())) {
                    const auto ctx_path = paths[i].string();
                    sched.submit(ctx_path, -static_cast<int64_t>(i), [&, ctx_path]() {
                        const auto res = _evaluate_context_file(ctx_path, epoch_cost_models, cfg);
                        ok.fetch_add(res.tx_ok, std::memory_order_relaxed);
                        err.fetch_add(res.tx_err, std::memory_order_relaxed);
                        mutex::scoped_lock lk { wits_mutex };
                        wits += res.wits;
                    });
                }
            }
            const auto res = sched.process_ok();
            logger::info("tx_ok: {}, tx_err: {} {}",
                ok.load(std::memory_order_relaxed), err.load(std::memory_order_relaxed),
                res ? "" : "some tasks have failed, so the counts can be incomplete");
            logger::info("validate tx witnesses: {}", wits);
        }
    private:
        struct user_config {
            std::optional<uint64_t> epoch {};
            std::optional<std::string> file {};
            std::optional<tx_hash> tx {};
            size_t num_workers = scheduler::default_worker_count();

            user_config(const options &opts)
            {
                if (const auto opt_it = opts.find("tx"); opt_it != opts.end() && opt_it->second)
                    tx = tx_hash::from_hex(*opt_it->second);
                if (const auto opt_it = opts.find("file"); opt_it != opts.end() && opt_it->second)
                    file = *opt_it->second;
                if (const auto opt_it = opts.find("epoch"); opt_it != opts.end() && opt_it->second)
                    epoch = std::stoull(*opt_it->second);
                if (const auto opt_it = opts.find("workers"); opt_it != opts.end() && opt_it->second)
                    num_workers = std::stoull(*opt_it->second);
            }
        };

        struct eval_result {
            size_t tx_ok = 0;
            size_t tx_err = 0;
            wit_cnt wits {};
        };

        struct parsed_models_update {
            uint64_t epoch;
            costs::parsed_models models;
        };
        using parsed_models_update_list = std::vector<parsed_models_update>;

        struct cost_model_update {
            uint64_t epoch;
            plutus_cost_models models;
        };
        using cost_model_update_list = std::vector<cost_model_update>;

        static eval_result _evaluate_context_file(const std::string &ctx_path, const parsed_models_update_list &epoch_cost_models, const user_config &cfg)
        {
            timer t { fmt::format("evaluation of a script context file {}", ctx_path), logger::level::info };
            logger::info("thread {} started testing context file {}", std::this_thread::get_id(), ctx_path);
            if (epoch_cost_models.empty()) [[unlikely]]
                throw error("epoch_cost_models must not be empty!");
            zpp_stream::read_stream rs { ctx_path };
            eval_result res {};
            while (!rs.eof()) {
                auto stored_ctx = rs.read<stored_tx_context>();
                const auto tx_id = stored_ctx.tx_id;
                if (cfg.tx && tx_id != *cfg.tx)
                    continue;
                std::optional<uint64_t> epoch {};
                try {
                    context ctx { std::move(stored_ctx) };
                    epoch.emplace(ctx.slot().epoch());
                    if (cfg.epoch && *epoch != *cfg.epoch)
                        continue;
                    auto it = std::upper_bound(epoch_cost_models.begin(), epoch_cost_models.end(), *epoch,
                        [&](const auto val, const auto &e) { return val < e.epoch; });
                    if (it == epoch_cost_models.begin())
                        throw error("internal error: can't find a passing epoch cost model!");
                    it = std::prev(it);
                    ctx.cost_models(it->models);
                    if (const auto *a_tx = dynamic_cast<const alonzo::tx *>(&ctx.tx()); a_tx)
                        res.wits += a_tx->witnesses_ok_plutus(ctx);
                    ++res.tx_ok;
                } catch (std::exception &ex) {
                    ++res.tx_err;
                    logger::warn("ctx: {} epoch: {} tx: {}: {}", ctx_path, epoch, tx_id, ex.what());
                }
            }
            const auto duration = t.stop(false);
            const auto scripts = res.wits.plutus_v1_script + res.wits.plutus_v2_script + res.wits.plutus_v3_script;
            logger::info("context {} evaluated in {:0.1f} sec tx_ok: {} tx_err: {} s-wits: {} perf: {:0.1f} s-wits/sec",
                ctx_path, duration, res.tx_ok, res.tx_err, scripts, static_cast<double>(scripts) / duration);
            return res;
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}
