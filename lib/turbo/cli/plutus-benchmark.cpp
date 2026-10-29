/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/mutex.hpp>
#include <turbo/common/progress.hpp>
#include <turbo/common/scheduler.hpp>
#include <turbo/json.hpp>
#include <turbo/plutus/context.hpp>
#include <turbo/plutus/cost-model-history.hpp>
#include <turbo/plutus/flat-encoder.hpp>
#include <turbo/plutus/machine.hpp>
#include "common.hpp"

namespace turbo::cli::plutus_benchmark {
    using namespace cardano;
    using namespace plutus;

    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "plutus-benchmark";
            cmd.desc = "run the plutus benchmark and save a CSV file with the results in <script-dir>/<run-id>-<thread-count>.csv";
            cmd.args.expect({ "<script-dir>", "<thread-count>", "<run-id>" });
        }

        void run(const arguments &args, const options &opts) const override {
            const auto &script_dir = args.at(0);
            const auto num_workers = std::stoull(args.at(1));
            const auto &run_id = args.at(2);
            const auto res_path = fmt::format("{}/{}-{}.csv", script_dir, run_id, num_workers);

            const auto epoch_cost_models = cost_model_history_t::from_json(json::load(fmt::format("{}/cost-models.json", script_dir)));
            epoch_cost_models.prepare();
            const auto paths = file::files_with_ext_path(script_dir, ".flat");
            static constexpr size_t batch_size = 1024;
            const auto num_batches = (paths.size() + batch_size - 1) / batch_size;
            scheduler sched { num_workers };
            mutex::unique_lock::mutex_type all_mutex alignas(mutex::alignment) {};
            script_res_map all {};
            std::atomic_size_t done = 0;
            for (size_t i = 0; i < paths.size(); i += batch_size) {
                sched.submit("extract", -static_cast<int64_t>(i), [&, i]() {
                    script_res_map res {};
                    std::optional<cost_model_history_t::active_models_t> cached_models{};
                    for (auto j = i, j_end = std::min(i + batch_size, paths.size()); j < j_end; ++j) {
                        const auto script_path = paths[j].string();
                        const auto bytes = file::read(script_path);
                        try {
                            const auto info = parse_name(paths[j].stem().string());
                            const auto epoch = from_str<uint64_t>(paths[j].parent_path().filename().string());
                            if (!cached_models || cached_models->epoch != epoch)
                                cached_models.emplace(epoch, epoch_cost_models.for_epoch(epoch));
                            const auto &model = cached_models->models.for_script(info.typ, builtins::semantics_variant(info.typ, info.protocol));
                            const auto start_time = std::chrono::high_resolution_clock::now();
                            allocator alloc{};
                            flat::script s{alloc, bytes, info.typ, info.protocol, false};
                            machine m{alloc, model, info.typ, info.budget, info.protocol};
                            const auto s_res = m.evaluate(s.program());
                            const auto run_time = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start_time).count();
                            res.try_emplace(script_path, flat::encode_cbor(s.version(), s_res.expr), run_time);
                        } catch (const std::exception &ex) {
                            throw error{"script {} (size: {}) failed: {}", script_path, bytes.size(), ex.what()};
                        }
                    }
                    {
                        mutex::scoped_lock lk { all_mutex };
                        for (auto &&[h, r]: res)
                            all.try_emplace(h, std::move(r));
                    }
                    const auto ok = done.fetch_add(1, std::memory_order_relaxed) + 1;
                    auto &p = progress::get();
                    p.update("plutus-benchmark", ok, num_batches);
                    p.inform();
                });
            }
            sched.process();
            save_results(res_path, all);
            logger::info("benchmarked scripts: {} using {} workers; the results were saved to {}", paths.size(), num_workers, res_path);
        }
    private:
        struct script_res {
            uint8_vector flat_res {};
            double run_time = 0.0;
        };
        using script_res_map = std::map<std::string, script_res>;

        struct script_info {
            tx_hash tx_id{};
            uint16_t redeemer_idx{};
            redeemer_tag redeemer_tag{};
            script_hash script_id{};
            script_type typ;
            uint64_t protocol;
            ex_units budget;
        };

        static uint64_t parse_field(const std::string_view text, const char prefix)
        {
            if (text.size() > 1 && text.front() == prefix)
                return from_str<uint64_t>(text.substr(1));
            throw error{"invalid '{}' field in script name: {}", prefix, text};
        }

        static void save_results(const std::string &res_path, const script_res_map &res)
        {
            std::string csv { "path,run_time,result\n" };
            auto csv_it = std::back_inserter(csv);
            for (const auto &[path, res]: res) {
                csv_it = fmt::format_to(csv_it, "{}, ", escape_utf8_string(path));
                csv_it = fmt::format_to(csv_it, "{}, ", res.run_time);
                csv_it = fmt::format_to(csv_it, "{}\n", res.flat_res);
            }
            file::write(res_path, csv);
        }

        static script_info parse_name(const std::string &stem)
        {
            std::stringstream ss{stem};
            std::vector<std::string> items{};
            std::string item{};
            while (std::getline (ss, item, '-')) {
                items.emplace_back(item);
            }
            if (items.size() != 8) [[unlikely]]
                throw error{"script name must encode 8 fields including memory and CPU budgets; re-extract scripts with older names: {}", stem};
            return {
                .tx_id = tx_hash::from_hex(items[0]),
                .redeemer_idx = numeric_cast<uint16_t>(std::stoul(items[1])),
                .redeemer_tag = redeemer_tag_from_str(items[2]),
                .script_id = script_hash::from_hex(items[3]),
                .typ = script_type_from_str(items[4]),
                .protocol = parse_field(items[5], 'p'),
                .budget = {parse_field(items[6], 'm'), parse_field(items[7], 's')}
            };
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}
