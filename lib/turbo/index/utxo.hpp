#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/index/common.hpp>

namespace turbo::index::utxo {
    struct chunk_indexer: chunk_indexer_one_epoch_base<cardano::txo_map> {
        using chunk_indexer_one_epoch_base::chunk_indexer_one_epoch_base;
    protected:
        void index_tx(const cardano::tx_base &tx) override
        {
            size_t txo_idx = 0;
            tx.foreach_output([&](const auto &tx_out) {
                _add_utxo(_data, tx, tx_out, txo_idx++);
            });
            tx.foreach_input([&](const auto &txi) {
                _del_utxo(_data, txi);
            });
        }

        void index_invalid_tx(const cardano::tx_base &tx) override;
    private:
        void _del_utxo(data_type &idx, const cardano::tx_out_ref &txo_id)
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

        static void _add_utxo(data_type &idx, const cardano::tx_base &tx, const cardano::tx_output &tx_out, const size_t txo_idx)
        {
            if (const auto [it, created] = idx.try_emplace(cardano::tx_out_ref { tx.hash(), txo_idx }, tx_out ); !created) [[unlikely]]
                throw error(fmt::format("found a non-unique TXO {}#{}", tx.hash(), txo_idx));
        }
    };
    using indexer = indexer_one_epoch<chunk_indexer>;
}