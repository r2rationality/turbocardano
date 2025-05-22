/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */
#ifndef DAEDALUS_TURBO_CARDANO_CONFIG_HPP
#define DAEDALUS_TURBO_CARDANO_CONFIG_HPP

#include <dt/config.hpp>
#include <dt/cardano/common/types.hpp>

namespace daedalus_turbo::cardano {
    struct config {
        const config_json byron_genesis;
        const block_hash byron_genesis_hash;
        const uint32_t byron_protocol_magic;
        const uint64_t byron_start_time;
        const uint64_t byron_epoch_length;
        const uint64_t byron_slot_duration;
        const txo_map byron_utxos;
        const set<vkey> byron_issuers;
        const set<key_hash> byron_delegate_hashes;
        const uint64_t byron_slots_per_chunk = 21600;
        const config_json shelley_genesis;
        const block_hash shelley_genesis_hash;
        const uint64_t shelley_epoch_length;
        const uint64_t shelley_update_quorum;
        const uint64_t shelley_max_lovelace_supply;
        const uint8_t shelley_network_id;
        const double shelley_active_slots;
        const uint64_t shelley_security_param;
        const uint64_t shelley_epoch_blocks;
        const uint64_t shelley_rewards_ready_slot;
        const uint64_t shelley_stability_window;
        const uint64_t shelley_randomness_stabilization_window;
        const uint64_t shelley_voting_deadline;
        const uint64_t shelley_chunks_per_epoch = shelley_epoch_length / byron_slots_per_chunk;
        const shelley_delegate_map shelley_delegates;
        const config_json alonzo_genesis;
        const block_hash alonzo_genesis_hash;
        const config_json conway_genesis;
        const block_hash conway_genesis_hash;
        const plutus_cost_models plutus_all_cost_models;
        const pool_voting_thresholds_t conway_pool_voting_thresholds;
        const drep_voting_thresholds_t conway_drep_voting_thresholds;

        static const config &get();
        explicit config(const configs &cfg=configs_dir::get());

        bool shelley_started() const
        {
            return static_cast<bool>(_shelley_start_slot);
        }

        void shelley_start_epoch(std::optional<uint64_t> epoch) const;

        uint64_t shelley_start_slot() const
        {
            if (_shelley_start_slot) [[likely]]
                return *_shelley_start_slot;
            return std::numeric_limits<uint64_t>::max();
        }

        uint64_t shelley_start_epoch() const
        {
            return shelley_start_slot() / byron_epoch_length;
        }

        uint64_t shelley_start_time() const
        {
            return byron_start_time + shelley_start_slot() * byron_slot_duration;
        }
    private:
        mutable std::optional<uint64_t> _shelley_start_slot {};

        static plutus_cost_models _prep_plutus_cost_models(const daedalus_turbo::config &genesis);
        static shelley_delegate_map _shelley_prep_delegates(const daedalus_turbo::config &genesis);
        static txo_map _byron_prep_utxos(const daedalus_turbo::config &genesis);
        static set<vkey> _byron_prep_heavy(const daedalus_turbo::config &genesis, std::string_view key);
        static set<key_hash> _byron_prep_hashes(const set<vkey> &);
        static block_hash _verify_hash_byron(const std::string_view &hash_hex, const daedalus_turbo::config &genesis);
        static block_hash _verify_hash(const std::string_view &hash_hex, const daedalus_turbo::config &genesis);
    };
}

#endif // !DAEDALUS_TURBO_CARDANO_CONFIG_HPP