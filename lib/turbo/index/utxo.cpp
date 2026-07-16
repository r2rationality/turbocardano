/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/babbage/block.hpp>
#include <turbo/index/utxo.hpp>

namespace turbo::index::utxo {
    void chunk_indexer::index_invalid_tx(const cardano::tx_base &tx)
    {
        // UTXOs used as collaterals are processed in validator.cpp:_apply_ledger_state_updates_for_epoch
        if (const auto *babbage_tx = dynamic_cast<const cardano::babbage::tx_base *>(&tx); babbage_tx) {
            if (const auto c_ret = babbage_tx->collateral_return(); c_ret) {
                // Use the virtual 1 past last normal tx output index
                const auto txo_idx = tx.outputs().size();
                _add_utxo(_data, tx, *c_ret, txo_idx);
            }
        }
    }
}
