/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cli/common.hpp>
#include <turbo/cardano.hpp>
#include <turbo/chunk-registry.hpp>

namespace turbo::cli::debug::tx_list {
    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "tx-list";
            cmd.desc = "list transactions ids";
            cmd.args.names.emplace_back("<chunk-path>");
            cmd.args.min = 1;
            cmd.opts.try_emplace("block", "print transactions only from a given block");
        }

        void run(const arguments &args, const options &opts) const override
        {
            std::optional<cardano::block_hash> block_id {};
            if (const auto opt_it = opts.find("block"); opt_it != opts.end() && opt_it->second)
                block_id.emplace(cardano::block_hash::from_hex(*opt_it->second));
            auto &sched = scheduler::get();
            std::mutex all_mutex alignas(mutex::alignment) {};
            info_list all {};
            for (const auto &chunk_path: args) {
                sched.submit("parse-chunk", 100, [&, chunk_path] {
                    const auto chunk = file::read(chunk_path);
                    cbor::zero2::decoder dec { chunk };
                    info_list extracted {};
                    while (!dec.done()) {
                        auto &block_tuple = dec.read();
                        const cardano::block_container blk { numeric_cast<uint64_t>(block_tuple.data_begin() - chunk.data()), block_tuple };
                        if (block_id && blk->hash() != *block_id)
                            continue;
                        blk->foreach_tx([&](const auto &tx) {
                            uint64_t out_amount = 0;
                            uint64_t num_plutus = 0;
                            size_t num_inputs = 0;
                            tx.foreach_input([&](const auto &) {
                                ++num_inputs;
                            });
                            size_t num_ref_inputs = 0;
                            tx.foreach_referenced_input([&](const auto &) {
                                ++num_ref_inputs;
                            });
                            size_t num_outs = 0;
                            tx.foreach_output([&](const auto &txo) {
                                ++num_outs;
                                out_amount += txo.coin;
                            });
                            tx.foreach_redeemer([&](const auto &) {
                                ++num_plutus;
                            });
                            extracted.emplace_back(blk->slot(), tx.index(), tx.hash(),
                                tx.size(), numeric_cast<uint16_t>(num_plutus), numeric_cast<uint16_t>(num_inputs),
                                numeric_cast<uint16_t>(num_ref_inputs), numeric_cast<uint16_t>(num_outs),
                                tx.fee(), out_amount);
                        });
                    }
                    std::scoped_lock lk { all_mutex };
                    all.insert(all.end(), extracted.begin(), extracted.end());
                    return extracted;
                });
            }
            sched.process(true);
            std::sort(all.begin(), all.end());
            for (const auto &c: all)
                std::cout << fmt::format("slot {}: tx index: {} tx hash: {} size: {} redeemers: {} inputs: {} ref_inputs: {} outputs: {} fee: {} out_amount: {} \n",
                    c.slot, c.tx_idx, c.tx_id, c.size, c.num_plutus, c.num_inputs, c.num_ref_inputs, c.num_outputs,
                    c.fee, c.out_amount);
        }
    private:
        struct extracted_info {
            uint64_t slot = 0;
            size_t tx_idx = 0;
            cardano::tx_hash tx_id {};
            size_t size = 0;
            uint16_t num_plutus = 0;
            uint16_t num_inputs = 0;
            uint16_t num_ref_inputs = 0;
            uint16_t num_outputs = 0;
            uint64_t fee = 0;
            uint64_t out_amount = 0;

            bool operator<(const auto &b) const
            {
                if (slot != b.slot)
                    return slot < b.slot;
                return tx_idx < b.tx_idx;
            }
        };
        using info_list = std::vector<extracted_info>;
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}