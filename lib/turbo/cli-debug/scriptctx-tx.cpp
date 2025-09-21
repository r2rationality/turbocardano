/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cli/common.hpp>
#include <turbo/history.hpp>
#include <turbo/plutus/context.hpp>
#include <turbo/zpp-stream.hpp>

namespace turbo::cli::scriptctx_tx {
    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "scriptctx-tx";
            cmd.desc = "extract the script context for a given transaction into a <tx-hash>.zstd file by default";
            cmd.args.expect({ "<data-dir>", "<tx-hash>", "[<output-file>]" });
        }

        void run(const arguments &args) const override
        {
            const auto &data_dir = args.at(0);
            const auto tx_hash = uint8_vector::from_hex(args.at(1));
            const auto &out_path = args.size() == 3 ? args.at(2) : fmt::format("{}.zpp", tx_hash);
            chunk_registry cr { data_dir, chunk_registry::mode::index };
            index::reader_multi_mt<index::tx::item> tx_reader { cr.indexer().reader_paths("tx") };
            reconstructor r { cr };
            const auto tx_info = r.find_tx(tx_hash);
            if (!tx_info) [[unlikely]]
                throw error(fmt::format("unknown transaction hash {}", tx_hash));
            auto block_tuple = cr.read((*tx_info)->block().offset());
            const cardano::block_container blk { (*tx_info)->block().offset(), block_tuple.get(), cr.config() };
            blk->foreach_tx([&](const auto &tx) {
                if (tx.hash() == tx_hash) {
                    plutus::stored_tx_context ctx {};
                    ctx.tx_id = tx_hash;
                    tx.foreach_redeemer([&](const auto &) {
                        ++ctx.num_redeemers;
                    });
                    ctx.body = tx.raw();
                    ctx.wits = tx.witness_raw();
                    ctx.block = cr.find_block_by_offset((*tx_info)->block().offset());
                    tx.foreach_input([&](const auto &txi) {
                        ctx.inputs.emplace_back(txi, _load_referenced_txo(cr, tx_reader, txi));
                    });
                    tx.foreach_referenced_input([&](const auto &txi) {
                        ctx.ref_inputs.emplace_back(txi, _load_referenced_txo(cr, tx_reader, txi));
                    });
                    zpp_stream::write_stream ws { out_path };
                    ws.write(ctx);
                }
            });
        }
    private:
        static cardano::tx_out_data _load_referenced_txo(const chunk_registry &cr, const index::reader_multi_mt<index::tx::item> &tx_reader, const cardano::tx_out_ref &txo_id)
        {
            thread_local auto tx_thread_data = tx_reader.init_thread();
            timer t { format("extract data for TXO {}", txo_id) };
            const index::tx::item search_tx { txo_id.hash };
            const auto [num_found, found_tx] = tx_reader.find(search_tx, tx_thread_data);
            if (num_found != 1) [[unlikely]]
                throw error(fmt::format("can't find tx {} in the tx index!", search_tx.hash));
            uint8_vector chunk_data {};
            const auto chunk_offset = cr.read_holding_chunk(chunk_data, found_tx.offset);
            auto tx_raw = cr.read_from_chunk_buffer(found_tx.offset, chunk_data, chunk_offset);
            auto tx_wit_raw = cr.read_from_chunk_buffer(found_tx.offset + found_tx.wit_rel_offset, chunk_data, chunk_offset);
            const auto block_meta = cr.find_block_by_offset(found_tx.offset);
            const auto ref_tx = cardano::tx_container { block_meta, found_tx.offset, tx_raw.get(), tx_wit_raw.get(), 0, cr.config() };
            std::optional<cardano::tx_out_data> res {};
            size_t txo_idx = 0;
            ref_tx->foreach_output([&](const auto &ref_txo) {
                if (txo_idx++ == txo_id.idx)
                    res.emplace(ref_txo);
            });
            if (!res) [[unlikely]]
                throw error(fmt::format("no output with index {} in tx: {}", txo_id.idx, txo_id.hash));
            return *res;
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}