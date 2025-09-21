/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

// Evaluation of the optimal chunk size
// 1. Blocks are already pre-sampled and split by era
// 2. Group blocks into batches based on the block count
// 3. Compress and measure ratio

// Evaluation of the optimal block size
// 1. Blocks are already pre-sampled and split by era
// 2. Group blocks into batches based on the block_size limit
// 3. Compress and measure ratio

#include <turbo/cli/common.hpp>
#include <turbo/cbor/zero2.hpp>
#include <turbo/common/mutex.hpp>
#include <turbo/common/progress.hpp>
#include <turbo/common/scheduler.hpp>
#include <turbo/common/zstd.hpp>

namespace turbo::cli::zstd_block_stats {

    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "zstd-block-stats";
            cmd.desc = "print compression stats about a sample of blocks";
            cmd.args.expect({ "<sample-dir>" });
        }

        void run(const arguments &args) const override
        {
            const auto paths = file::files_with_ext(args.at(0), ".block");
            logger::info("found original blocks: {}", paths.size());
            mutex::unique_lock::mutex_type res_mutex alignas(mutex::alignment) {};
            std::map<size_t, stats> res {};
            auto &sched = scheduler::get();
            for (const auto max_block_size: { 90112, 256 * 1024, 1024 * 1024, 4 * 1024 * 1024, 16 * 1024 * 1024, 64 * 1024 * 1024 }) {
                sched.submit("zstd-block-stats", 100, [max_block_size, &paths, &res_mutex, &res] {
                    auto s = _compute_stats(paths, max_block_size);
                    mutex::scoped_lock lk { res_mutex };
                    res[max_block_size] = std::move(s);
                });
            }
            sched.process();
            for (const auto &[max_block_size, stats]: res) {
                logger::info("{}: {}", max_block_size, stats.to_string());
            }
        }
    private:
        struct stats {
            struct zstd_info {
                size_t compressed_size;
                double compression_time;

                zstd_info operator+=(const zstd_info &o)
                {
                    compressed_size += o.compressed_size;
                    compression_time += o.compression_time;
                    return *this;
                }
            };

            std::map<int, zstd_info> levels {};
            size_t raw_size = 0;
            size_t num_blocks = 0;

            void add(const buffer bytes)
            {
                static const std::vector<int> zstd_levels { 1, 3, 9, 10, 11, 12, 15, 22 };
                raw_size += bytes.size();
                ++num_blocks;
                for (const auto zstd_level: zstd_levels) {
                    const auto start = std::chrono::high_resolution_clock::now();
                    const auto compressed = zstd::compress(bytes, zstd_level);
                    levels[zstd_level] += zstd_info { compressed.size(), std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start).count() };
                }
            }

            stats &operator+=(const stats &o)
            {
                num_blocks += o.num_blocks;
                raw_size += o.raw_size;
                for (const auto &[level, info]: o.levels) {
                    levels[level] += info;
                }
                return *this;
            }

            std::string to_string() const
            {
                std::string res {};
                auto res_it = std::back_inserter(res);
                res_it = fmt::format_to(res_it, "stats from {} blocks", num_blocks);
                if (raw_size) [[likely]] {
                    res_it = fmt::format_to(res_it, " of {} bytes on average:\n", raw_size / num_blocks);
                    for (const auto &[level, info]: levels) {
                        res_it = fmt::format_to(res_it,
                            "        -> zstd level {}: ratio: {:0.3f} throughput: {:0.1f} MB/sec compression time: {:0.3f} sec transmission time: {:0.3f} sec\n",
                            level, static_cast<double>(raw_size) / info.compressed_size,
                            (static_cast<double>(raw_size) / 1'000'000) / info.compression_time,
                            info.compression_time / num_blocks,
                            static_cast<double>(info.compressed_size) / num_blocks / 10e6 // (a transmission over unsaturated 100 Mbps channel)
                        );
                    }
                } else {
                    res_it = fmt::format_to(res_it, "\n");
                }
                return res;
            }
        };

        static stats _compute_stats(const std::vector<std::string> &paths, const size_t max_block_size)
        {
            struct tx_info {
                buffer data {};
                buffer wit {};
                buffer aux {};
            };

            struct block_info {
                buffer hdr;
                std::vector<tx_info> txs {};

                block_info(const buffer data)
                {
                    auto pv = cbor::zero2::parse(data);
                    auto &block_tuple = pv.get();
                    auto &bt_it = block_tuple.array();
                    bt_it.skip(1);
                    //const auto era = bt_it.read().uint();
                    auto &blk = bt_it.read();
                    auto &blk_it = blk.array();
                    hdr = blk_it.read().data_raw();
                    {
                        auto &txs_v = blk_it.read();
                        auto &t_it = txs_v.array();
                        while (!t_it.done()) {
                            txs.emplace_back(t_it.read().data_raw());
                        }
                    }
                    {
                        auto &wits = blk_it.read();
                        auto &w_it = wits.array();
                        for (size_t i = 0; !w_it.done(); ++i) {
                            if (i >= txs.size()) [[unlikely]]
                                throw error(fmt::format("witness for tx #{} while the number of txs is {}!", i, txs.size()));
                            txs[i].wit = w_it.read().data_raw();
                        }
                    }
                    {
                        auto &aux = blk_it.read();
                        auto &a_it = aux.map();
                        while (!a_it.done()) {
                            auto &key = a_it.read_key();
                            const auto i = key.uint();
                            if (i >= txs.size()) [[unlikely]]
                                throw error(fmt::format("metadata for tx #{} while the number of txs is {}!", i, txs.size()));
                            txs[i].aux = a_it.read_val(std::move(key)).data_raw();
                        }
                    }
                }
            };

            struct batch_info {
                std::optional<uint8_vector> hdr {};
                uint8_vector txs {};
                uint8_vector wits {};
                uint8_vector aux {};
                size_t num_txs = 0;

                size_t size() const
                {
                    if (hdr) [[likely]] {
                        return hdr->size() + txs.size() + wits.size() + aux.size();
                    }
                    return 0;
                }

                bool can_add(const tx_info &tx, const size_t max_block_size) const
                {
                    const auto new_size = size() + tx.data.size() + tx.wit.size() + tx.aux.size();
                    return new_size <= max_block_size;
                }

                void add(const buffer &new_hdr, const tx_info &tx)
                {
                    if (!hdr)
                        hdr.emplace(new_hdr);
                    txs << tx.data;
                    wits << tx.wit;
                    aux << tx.aux;
                }

                void clear()
                {
                    hdr.reset();
                    txs.clear();
                    wits.clear();
                    aux.clear();
                }

                uint8_vector data() const
                {
                    if (!hdr) [[unlikely]]
                        throw error("batch cannot be empty!");
                    uint8_vector res {};
                    res.reserve(hdr->size() + txs.size() + wits.size() + aux.size());
                    res << *hdr << txs << wits << aux;
                    return res;
                }
            };

            const std::string name = fmt::format("compute-stats-{}", max_block_size);
            std::atomic_size_t num_ready { 0 };
            stats all {};

            batch_info batch {};
            for (const auto &blk_path: paths) {
                try {
                    const auto blk_data = file::read(blk_path);
                    const block_info blk { blk_data };
                    for (const auto &tx: blk.txs) {
                        if (!batch.can_add(tx,  max_block_size)) {
                            all.add(batch.data());
                            batch.clear();
                        }
                        batch.add(blk.hdr, tx);
                    }
                    const auto done = num_ready.fetch_add(1, std::memory_order_relaxed) + 1;
                    progress::get().update(name, done, paths.size());
                    progress::get().inform();
                } catch (std::exception &e) {
                    throw error(fmt::format("an error while parsing {}: {}", blk_path, e.what()));
                }
            }
            // the last batch is discarded since incomplete!

            return all;
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}
