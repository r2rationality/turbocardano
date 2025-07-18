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
#include <turbo/plutus/costs.hpp>
#include <turbo/plutus/flat-encoder.hpp>
#include <turbo/zpp-stream.hpp>
#include "common.hpp"

namespace turbo::cli::plutus_extract_scripts {
    using namespace cardano;
    using namespace cardano::ledger;
    using namespace plutus;

    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "plutus-extract-scripts";
            cmd.desc = "extract plutus scripts after pre-applying their arguments";
            cmd.args.expect({ "<ctx-dir>", "<tx-list-path|tx-hash>", "<output-dir>" });
            cmd.opts.try_emplace("uplc", "extract scripts in UPLC format");
            cmd.opts.try_emplace("file", "consider only files starting with a given prefix");
        }

        void run(const arguments &args, const options &opts) const override {
            const auto &ctx_dir = args.at(0);
            const auto &tx_list_path = args.at(1);
            const auto &out_dir = args.at(2);
            std::filesystem::create_directories(out_dir);
            user_config cfg { .uplc=opts.contains("uplc") };
            {
                cfg.tx_list.emplace();
                if (std::filesystem::exists(tx_list_path)) {
                    const std::string text { file::read(tx_list_path).str() };
                    for (auto prev_it = text.begin(); prev_it != text.end(); ) {
                        auto next_it = std::find_if(prev_it, text.end(), [&](const auto &x) { return std::isspace(x); });
                        if (const std::string tx_id { prev_it, next_it }; !tx_id.empty())
                            cfg.tx_list->emplace(tx_hash::from_hex(tx_id));
                        while (next_it != text.end() && std::isspace(*next_it))
                            ++next_it;
                        prev_it = next_it;
                    }
                } else {
                    cfg.tx_list->emplace(tx_hash::from_hex(tx_list_path));
                }
            }

            auto &sched = scheduler::get();
            std::optional<std::string> file_prefix {};
            if (const auto opt_it = opts.find("file"); opt_it != opts.end() && opt_it->second)
                file_prefix.emplace(*opt_it->second);
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
                if (!file_prefix || p.filename().string().starts_with(*file_prefix))
                    paths.emplace_back(std::move(p));
            }
            std::sort(paths.begin(), paths.end());

            mutex::unique_lock::mutex_type all_mutex alignas(mutex::alignment) {};
            extract_res all {};
            for (size_t i = 0; i < paths.size(); ++i) {
                if (std::filesystem::file_size(paths[i]) > 0) {
                    const auto ctx_path = paths[i].string();
                    sched.submit("extract", -static_cast<int64_t>(i), [&, ctx_path]() {
                        const auto res = _extract_scripts(out_dir, ctx_path, epoch_cost_models, cfg);
                        mutex::scoped_lock lk { all_mutex };
                        all += res;
                    });
                }
            }
            sched.process();
            logger::info("extracted unique scripts: {} redeemers: {}", all.scripts.size(), all.num_redeemers);
        }
    private:
        struct user_config {
            std::optional<std::set<tx_hash>> tx_list {};
            bool uplc = false;
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

        struct extract_res {
            std::set<script_hash> scripts {};
            size_t num_redeemers = 0;

            extract_res &operator+=(const extract_res &o)
            {
                num_redeemers += o.num_redeemers;
                for (const auto &h: o.scripts)
                    scripts.emplace(h);
                return *this;
            }
        };

        static extract_res _extract_scripts(const std::string &out_dir, const std::string &ctx_path, const parsed_models_update_list &epoch_cost_models, const user_config &cfg)
        {
            timer t { fmt::format("evaluation of a script context file {}", ctx_path), logger::level::info };
            logger::info("thread {} started testing context file {}", std::this_thread::get_id(), ctx_path);
            if (epoch_cost_models.empty()) [[unlikely]]
                throw error("epoch_cost_models must not be empty!");
            zpp_stream::read_stream rs { ctx_path };
            extract_res res {};
            while (!rs.eof()) {
                auto stored_ctx = rs.read<stored_tx_context>();
                const auto &tx_id = stored_ctx.tx_id;
                if (cfg.tx_list && !cfg.tx_list->contains(tx_id))
                    continue;
                std::optional<uint64_t> epoch {};
                try {
                    context ctx { std::move(stored_ctx) };
                    epoch.emplace(ctx.slot().epoch());
                    auto it = std::upper_bound(epoch_cost_models.begin(), epoch_cost_models.end(), *epoch,
                        [&](const auto val, const auto &e) { return val < e.epoch; });
                    if (it == epoch_cost_models.begin())
                        throw error("internal error: can't find a passing epoch cost model!");
                    it = std::prev(it);
                    ctx.cost_models(it->models);
                    size_t r_idx = 0;
                    for (const auto &[rid, r]: ctx.redeemers()) {
                        const auto ps = ctx.prepare_script(r);
                        const auto typ_s = fmt::format("{}", ps.typ);
                        static std::string exp_prefix { "plutus_" };
                        if (!typ_s.starts_with(exp_prefix)) [[unlikely]]
                            throw error(fmt::format("unsupported script type: {}!", typ_s));
                        const auto out_prefix = fmt::format("{}/{}/{}-{}-{}-{}",
                            out_dir, *epoch, tx_id, r_idx++, ps.hash, typ_s.substr(exp_prefix.size()));
                        if (cfg.uplc) {
                            file::write(out_prefix + ".uplc", fmt::format("(program 0.0.0 {})", ps.expr));
                        } else {
                            file::write(out_prefix + ".flat", flat::encode_cbor(ps.ver, ps.expr));
                        }
                        ++res.num_redeemers;
                    }
                    for (const auto &[hash, script]: ctx.scripts())
                        res.scripts.emplace(hash);
                } catch (std::exception &ex) {
                    throw error(fmt::format("ctx: {} epoch: {} tx: {}: {}", ctx_path, epoch, tx_id, ex.what()));
                }
            }
            return res;
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}
