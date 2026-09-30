/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.BlockBody.
// Included at namespace scope by lib/turbo/cardano/ledger/shelley.cpp.

    void state::_process_block_updates(block_update_list &&block_updates)
    {
        std::map<pool_hash, size_t> pool_blocks {};
        for (const auto &bu: block_updates) {
            add_fees(bu.fees);
            process_block(bu.end_offset, bu.slot);
            if (bu.era > 1)
                ++pool_blocks[bu.issuer_id];
        }
        for (const auto &[pool_id, num_blocks]: pool_blocks)
            add_pool_blocks(pool_id, num_blocks);
    }

    void state::add_pool_blocks(const cardano::pool_hash &pool_id, uint64_t num_blocks)
    {
        if (!_pbft_pools.contains(pool_id)) {
            if (_operating_stake_dist.contains(pool_id)) {
                _blocks_current.add(pool_id, num_blocks);
            } else {
                logger::warn("trying to provide the number of generated blocks in epoch {} for an unknown pool {} num_blocks: {}!", _epoch, pool_id, num_blocks);
            }
        }
    }

    void state::process_block(const uint64_t end_offset, const uint64_t slot)
    {
        if (end_offset > _end_offset) {
            _end_offset = end_offset;
        }
        const auto epoch_slot = cardano::slot { slot, _cfg }.epoch_slot();
        if (_params.protocol_ver.major >= 2 && epoch_slot >= _cfg.shelley_voting_deadline) {
            ++_blocks_past_voting_deadline;
        }
        if (epoch_slot > _epoch_slot) {
            _epoch_slot = epoch_slot;
        }
    }
