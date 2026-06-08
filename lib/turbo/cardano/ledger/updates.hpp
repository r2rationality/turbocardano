#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cardano/common/common.hpp>
#include <turbo/cardano/ledger/types.hpp>
#include <turbo/index/block-fees.hpp>
#include <turbo/index/timed-update.hpp>

namespace turbo::cardano::ledger {
    struct block_update_list: std::vector<index::block_fees::item> {
        using vector::vector;
    };

    struct utxo_update_list: std::vector<txo_map> {
        using vector::vector;
    };

    struct timed_update_t: index::timed_update::item {
    };

    struct timed_update_list: std::vector<timed_update_t> {
        using vector::vector;
    };

    struct update_effects_t {
        tx_out_ref_list collected_collateral {};
        uint64_t collateral_refund = 0;
    };

    struct updates_t {
        block_update_list blocks {};
        utxo_update_list utxos {};
        timed_update_list timed {};
    };
}
