/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/ledger/state.hpp>
#include <turbo/chunk-registry.hpp>
#include <turbo/common/file.hpp>
#include <turbo/index/utxo.hpp>
#include <turbo/plutus/context.hpp>
#include <turbo/plutus/flat.hpp>
#include <turbo/storage/partition.hpp>
#include <array>
#include <set>
#include <vector>
#include "common.hpp"

namespace turbo::cli::txwit_stat {
    using namespace cardano;
    using namespace cardano::ledger;
    using namespace plutus;

    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "txwit-stat";
            cmd.desc = "Print statistics tx witnesses";
            cmd.args.expect({ "<data-dir>" });
            cmd.opts.try_emplace("epoch", "count witnesses only in the given epoch");
            cmd.opts.try_emplace("builtin-stats", "count static occurrences of each Plutus builtin across unique scripts");
            cmd.opts.try_emplace("scripts-dir", "write unique Plutus scripts as <hash>-<script-type>.flat files into this directory");
        }

        void run(const arguments &args, const options &opts) const override
        {
            const auto &data_dir = args.at(0);
            const bool builtin_stats = opts.contains("builtin-stats");
            std::optional<std::filesystem::path> scripts_dir{};
            if (const auto it = opts.find("scripts-dir"); it != opts.end()) {
                if (!it->second || it->second->empty())
                    throw error { "--scripts-dir requires a non-empty directory path" };
                scripts_dir.emplace(*it->second);
                std::filesystem::create_directories(*scripts_dir);
            }
            const chunk_registry cr{data_dir, chunk_registry_settings_t{.mode=chunk_registry::mode::store}};
            std::optional<uint64_t> epoch{};
            if (const auto it = opts.find("epoch"); it != opts.end() && it->second)
                epoch.emplace(std::stoull(*it->second));
            const storage::partition_map partitions{cr, {.first_epoch=epoch, .last_epoch=epoch, .num_parts=1024}};
            mutex::unique_lock::mutex_type all_mutex alignas(mutex::alignment) {};
            part_info all{};
            storage::parse_parallel<part_info>(cr, partitions,
                [&](auto &part, const auto &blk) {
                    ++part.num_blocks;
                    blk->foreach_tx([&](const auto &tx) {
                        ++part.num_txs;
                        tx.foreach_redeemer([&](const auto &) {
                            ++part.num_redeemers;
                        });
                        tx.foreach_witness([&](const auto &wit) {
                            std::visit([&](const auto &wv) {
                                using T = std::decay_t<decltype(wv)>;
                                if constexpr (std::is_same_v<T, tx_wit_byron_vkey> || std::is_same_v<T, tx_wit_byron_redeemer>
                                        || std::is_same_v<T, tx_wit_shelley_vkey> || std::is_same_v<T, tx_wit_shelley_bootstrap>) {
                                    ++part.num_vkey;
                                } else if constexpr (std::is_same_v<T, script_info>) {
                                    part.scripts[wv.type()].try_emplace(wv.hash(), wv.script());
                                    if (wv.type() == script_type::native)
                                        ++part.num_native;
                                }
                            }, wit);
                        });
                        tx.foreach_output([&](const auto &txo) {
                            if (const auto pay = txo.addr().pay_id(); pay.type == pay_ident::ident_type::SHELLEY_SCRIPT)
                                part.output_script_refs.emplace(pay.hash);
                            if (txo.script_ref) {
                                const auto &s = *txo.script_ref;
                                part.scripts[s.type()].try_emplace(s.hash(), s.script());
                            }
                        });
                    });
                },
                [&](auto, const auto &) {
                    return part_info{};
                },
                [&](auto &&part, const auto, const auto &) {
                    mutex::scoped_lock lk{all_mutex};
                    all += std::move(part);
                },
                "count-witnesses"
            );
            auto &sched = cr.sched();
            builtin_counts counts{};
            size_t num_failed_scripts = 0;
            if (scripts_dir)
                logger::info("saving plutus scripts into {}", *scripts_dir);
            for (const auto &[typ, srcs]: all.scripts) {
                for (const auto &[hash, blob]: srcs)
                    all.output_script_refs.erase(hash);
                if (builtin_stats && typ != script_type::native) {
                    for (const auto &[h, blob]: srcs) {
                        sched.submit("count-builtins", 0, [&, h, typ, script=buffer{blob}] {
                            try {
                                const auto script_counts = count_builtins(script);
                                mutex::scoped_lock lk{all_mutex};
                                for (size_t i = 0; i < counts.size(); ++i)
                                    counts[i] += script_counts[i];
                            } catch (const std::exception &ex) {
                                logger::debug("cannot count builtins in script {} ({}): {}", h, typ, ex.what());
                                mutex::scoped_lock lk{all_mutex};
                                ++num_failed_scripts;
                            }
                        });
                    }
                }
                if (scripts_dir && typ != script_type::native) {
                    for (const auto &[h, blob]: srcs) {
                        sched.submit("write-scripts", 0, [path=*scripts_dir / fmt::format("{}-{}.flat", h, typ), script=buffer{blob}] {
                            file::write(path, script);
                        });
                    }
                }
                logger::info("  {}: {}", typ, srcs.size());
            }
            if (scripts_dir || builtin_stats)
                sched.process();
            if (scripts_dir)
                logger::info("saved plutus scripts: {}", all.num_plutus());
            if (builtin_stats) {
                logger::info("Plutus builtin occurrences across {} successfully parsed unique scripts ({} failed parsing):",
                    all.num_plutus() - num_failed_scripts, num_failed_scripts);
                for (size_t i = 0; i < counts.size(); ++i)
                    logger::info("  {}: {}", static_cast<builtin_tag>(i), counts[i]);
            }
            logger::info("unique scripts referenced by output payment addresses but absent from scanned scripts: {}",
                all.output_script_refs.size());
            logger::info("blocks: {} txs: {} redeemers: {} native: {} vkey: {} unique plutus scripts: {}",
                all.num_blocks, all.num_txs, all.num_redeemers, all.num_native, all.num_vkey, all.num_plutus());
        }
    private:
        using builtin_counts = std::array<size_t, builtin_tag_count>;

        static builtin_counts count_builtins(const buffer bytes)
        {
            allocator alloc{};
            const flat::script script{alloc, bytes};
            builtin_counts counts{};
            std::vector<term> pending{script.program()};
            while (!pending.empty()) {
                const auto expr = pending.back();
                pending.pop_back();
                expr.visit([&](const auto &node) {
                    using T = std::decay_t<decltype(node)>;
                    if constexpr (std::is_same_v<T, t_builtin>) {
                        ++counts[static_cast<size_t>(node.tag)];
                    } else if constexpr (std::is_same_v<T, t_builtin_spine>) {
                        ++counts[static_cast<size_t>(node.b.tag)];
                        pending.insert(pending.end(), node.args.begin(), node.args.end());
                    } else if constexpr (std::is_same_v<T, force> || std::is_same_v<T, t_delay> || std::is_same_v<T, t_lambda>) {
                        pending.emplace_back(node.expr);
                    } else if constexpr (std::is_same_v<T, apply>) {
                        pending.emplace_back(node.func);
                        pending.emplace_back(node.arg);
                    } else if constexpr (std::is_same_v<T, t_constr>) {
                        pending.insert(pending.end(), node.args->begin(), node.args->end());
                    } else if constexpr (std::is_same_v<T, t_case>) {
                        pending.emplace_back(node.arg);
                        pending.insert(pending.end(), node.cases->begin(), node.cases->end());
                    }
                });
            }
            return counts;
        }

        struct part_info {
            size_t num_blocks = 0;
            size_t num_txs = 0;
            size_t num_redeemers = 0;
            size_t num_native = 0;
            size_t num_vkey = 0;
            std::map<script_type, std::map<script_hash, uint8_vector>> scripts{};
            std::set<script_hash> output_script_refs{};

            size_t num_plutus() const
            {
                size_t count = 0;
                for (const auto &[typ, srcs]: scripts) {
                    if (typ != script_type::native)
                        count += srcs.size();
                }
                return count;
            }

            part_info &operator+=(part_info &&o) {
                num_blocks += o.num_blocks;
                num_txs += o.num_txs;
                num_redeemers += o.num_redeemers;
                num_native += o.num_native;
                num_vkey += o.num_vkey;
                for (auto &&[typ, srcs]: o.scripts)
                    scripts[typ].merge(std::move(srcs));
                output_script_refs.merge(std::move(o.output_script_refs));
                return *this;
            }
        };
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}
