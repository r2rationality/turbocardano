/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cli/common.hpp>
#include <turbo/cardano.hpp>
#include <turbo/common/scheduler.hpp>
#include <turbo/crypto/crc32.hpp>
#include <turbo/util.hpp>

namespace turbo::cli::debug::block_list {
    struct cmd: command {
        const command_info &info() const override
        {
            static const command_info i { "block-list", "<chunk-path> [<chunk-path> ...]", "print per block information from a list of chunks" };
            return i;
        }

        void run(const arguments &args) const override
        {
            if (args.size() == 0) _throw_usage();
            std::mutex chunks_mutex alignas(mutex::alignment) {};
            std::map<std::string, block_list> chunks {};
            auto &sched = scheduler::get();
            for (const auto &chunk_path: args) {
                sched.submit("block-info", 0, [&chunks_mutex, &chunks, chunk_path] {
                    try {
                        const auto chunk = file::read_auto(chunk_path);
                        cbor::zero2::decoder dec { chunk };
                        block_list blocks {};
                        for (size_t block_idx = 0; !dec.done(); ++block_idx) {
                            try {
                                auto &block_tuple = dec.read();
                                const cardano::block_container blk { numeric_cast<uint64_t>(block_tuple.data_begin() - chunk.data()), block_tuple };
                                uint64_t mir_rewards = 0;
                                uint64_t withdrawals = 0;
                                size_t num_txs = 0;
                                blk->foreach_tx([&](const auto &tx) {
                                    ++num_txs;
                                    tx.foreach_cert([&](const auto &c) {
                                        std::visit([&](const auto &cv) {
                                            using T = std::decay_t<decltype(cv)>;
                                            if constexpr (std::is_same_v<T, cardano::instant_reward_cert>) {
                                                for (const auto &[stake_id, amount]: cv.rewards)
                                                    mir_rewards += amount;
                                            }
                                        }, c.val);
                                    });
                                    tx.foreach_withdrawal([&](const auto &with) {
                                        withdrawals += with.amount;
                                    });
                                });
                                size_t num_invalid_txs = 0;
                                blk->foreach_invalid_tx([&](const auto &) {
                                    ++num_invalid_txs;
                                });
                                blocks.emplace_back(blk->era(), blk->slot_object(), blk->height(), blk->hash(), blk->prev_hash(), blk->issuer_vkey(), blk->issuer_hash(), blk->protocol_ver(),
                                    mir_rewards, withdrawals, blk.offset(), blk.size(), crypto::crc32::digest(blk.raw()),
                                    numeric_cast<uint16_t>(blk->header_offset()),
                                    numeric_cast<uint16_t>(blk->header().size()),
                                    num_txs, num_invalid_txs);
                            } catch (const std::exception &ex) {
                                throw error(fmt::format("parsing block #{} failed!", block_idx), ex);
                            }
                        }
                        std::scoped_lock lk { chunks_mutex };
                        chunks[chunk_path] = std::move(blocks);
                    } catch (const std::exception &ex) {
                        throw error(fmt::format("can't parse chunk {}!", chunk_path), ex);
                    }
                });
            }
            sched.process();
            for (const auto &[chunk_path, chunk_blocks]: chunks) {
                auto filename = std::filesystem::path { chunk_path }.filename().string();
                size_t i = 0;
                for (const auto &b: chunk_blocks)
                    std::cout << fmt::format("{} #{} offset:{} size:{} era: {} slot: {} height: {} hash: {} prev: {} issuer_vkey: {} issuer_hash: {} prot_ver: {} mir: {} withdrawals: {} "
                            "check_sum: {:#8x} header_off: {} header_size: {} txs: {} invalid_txs: {}\n",
                                    filename, i++, b.offset, b.size, b.era, b.slot, b.height, b.hash, b.prev_hash, b.issuer_vkey, b.pool_id, b.protocol_ver,
                                    b.mir_rewards, b.withdrawals, b.check_sum, b.header_offset, b.header_size, b.num_txs, b.num_invalid_txs);
            }
        }
    private:
        struct block_info {
            uint64_t era = 0;
            cardano::slot slot;
            uint64_t height = 0;
            cardano::block_hash hash {};
            std::optional<cardano::block_hash> prev_hash {};
            cardano::vkey issuer_vkey {};
            cardano::pool_hash pool_id {};
            cardano::protocol_version protocol_ver {};
            uint64_t mir_rewards = 0;
            uint64_t withdrawals = 0;
            uint64_t offset = 0;
            uint32_t size = 0;
            uint32_t check_sum = 0;
            uint16_t header_offset = 0;
            uint16_t header_size = 0;
            size_t num_txs = 0;
            size_t num_invalid_txs = 0;
        };
        using block_list = std::vector<block_info>;
        using chunk_block_info = std::pair<std::string, block_list>;
    };

    static auto instance = command::reg(std::make_shared<cmd>());
}