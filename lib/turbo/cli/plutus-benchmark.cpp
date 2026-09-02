/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/mutex.hpp>
#include <turbo/common/progress.hpp>
#include <turbo/common/scheduler.hpp>
#include <turbo/plutus/context.hpp>
#include <turbo/plutus/costs.hpp>
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
            cmd.opts.try_emplace("protocol",
                "Cardano major protocol version for legacy script names; 'auto' infers it from a mainnet epoch directory",
                "auto");
        }

        void run(const arguments &args, const options &opts) const override {
            const auto &script_dir = args.at(0);
            const auto num_workers = std::stoull(args.at(1));
            const auto &run_id = args.at(2);
            const auto fallback_protocol = parse_protocol_option(opts.at("protocol").value());
            const auto res_path = fmt::format("{}/{}-{}.csv", script_dir, run_id, num_workers);

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
                    for (auto j = i, j_end = std::min(i + batch_size, paths.size()); j < j_end; ++j) {
                        const auto script_path = paths[j].string();
                        const auto bytes = file::read(script_path);
                        try {
                            const auto info = parse_name(paths[j].stem().string());
                            const auto protocol_major = resolve_protocol(paths[j], info, fallback_protocol);
                            const auto start_time = std::chrono::high_resolution_clock::now();
                            allocator alloc {};
                            flat::script s { alloc, bytes, info.typ, protocol_major, true };
                            machine m { alloc, info.typ, {}, protocol_major };
                            const auto s_res = m.evaluate(s.program());
                            const auto run_time = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start_time).count();
                            res.try_emplace(script_path, flat::encode_cbor(s.version(), s_res.expr), run_time);
                        } catch (const std::exception &ex) {
                            throw error(fmt::format("script {} (size: {}) failed: {}", script_path, bytes.size(), ex.what()));
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
            tx_hash tx_id {};
            script_hash script_id {};
            uint16_t redeemer_idx {};
            script_type typ;
            std::optional<uint64_t> protocol {};
        };

        static uint64_t parse_protocol(const std::string &text)
        {
            size_t parsed = 0;
            try {
                const auto protocol = std::stoull(text, &parsed);
                if (parsed == text.size())
                    return protocol;
            } catch (const std::exception &) {
            }
            throw error(fmt::format("unsupported protocol version: {}", text));
        }

        static std::optional<uint64_t> parse_protocol_option(const std::string &text)
        {
            if (text == "auto")
                return {};
            return parse_protocol(text);
        }

        static uint64_t infer_mainnet_protocol(const std::filesystem::path &path)
        {
            const auto epoch_text = path.parent_path().filename().string();
            size_t parsed = 0;
            uint64_t epoch = 0;
            try {
                epoch = std::stoull(epoch_text, &parsed);
            } catch (const std::exception &) {
            }
            if (epoch_text.empty() || parsed != epoch_text.size()) [[unlikely]] {
                throw error(fmt::format(
                    "cannot infer the protocol version for legacy script {}: its parent directory '{}' is not a mainnet epoch; "
                    "use --protocol=N or re-extract the scripts",
                    path.string(), epoch_text));
            }

            // Mainnet protocol-version epoch boundaries through the last interval
            // that can be inferred unambiguously without metadata in the file name.
            if (epoch < 290) [[unlikely]] {
                throw error(fmt::format(
                    "cannot infer a Plutus protocol version for legacy script {} from pre-Alonzo mainnet epoch {}",
                    path.string(), epoch));
            }
            if (epoch < 298)
                return 5;
            if (epoch < 365)
                return 6;
            if (epoch < 394)
                return 7;
            if (epoch < 507)
                return 8;
            if (epoch <= 536)
                return 9;
            throw error(fmt::format(
                "cannot safely infer the protocol version for legacy script {} from mainnet epoch {}; "
                "use --protocol=N or re-extract the scripts",
                path.string(), epoch));
        }

        static uint64_t resolve_protocol(const std::filesystem::path &path, const script_info &info,
            const std::optional<uint64_t> &fallback)
        {
            if (info.protocol)
                return *info.protocol;
            if (fallback)
                return *fallback;
            return infer_mainnet_protocol(path);
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
            std::stringstream ss { stem };
            std::vector<std::string> items {};
            std::string item {};
            while (std::getline (ss, item, '-')) {
                items.emplace_back(item);
            }
            if (items.size() != 4 && items.size() != 5) [[unlikely]]
                throw error(fmt::format("script name must encode 4 or 5 fields but got: {}", stem));
            std::optional<uint64_t> protocol {};
            if (items.size() == 5) {
                if (!items[4].starts_with('p')) [[unlikely]]
                    throw error(fmt::format("invalid protocol field in script name: {}", stem));
                protocol.emplace(parse_protocol(items[4].substr(1)));
            }
            return { tx_hash::from_hex(items[0]), script_hash::from_hex(items[2]),
                numeric_cast<uint16_t>(std::stoul(items[1])), script_type_from_str(items[3]), protocol };
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}
