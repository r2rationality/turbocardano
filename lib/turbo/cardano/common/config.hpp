#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <memory>
#include <turbo/cardano/common/cert.hpp>
#include <turbo/config.hpp>

namespace turbo::cardano {
    using vkey_set = flat_set<vkey>;
    struct byron_delegate_info {
        crypto::ed25519::vkey_full issuer {};
        crypto::ed25519::vkey_full delegate {};
        crypto::ed25519::signature certificate {};
        uint64_t epoch = 0;
        uint8_vector epoch_cbor {};
    };
    using byron_delegate_map = flat_map<vkey, byron_delegate_info>;
    using committee_member_map = flat_map<credential_t, uint64_t>;

    struct shelley_initial_state_t {
        txo_map utxos {};
        flat_map<pool_hash, pool_params> pools {};
        flat_map<stake_keyhash_t, pool_hash> delegations {};
    };

    // Genesis inputs only: loading these does not activate ledger or consensus rules.
    struct dijkstra_genesis_t {
        block_hash hash {};
        uint32_t leios_announcement_period_length = 0;
        uint16_t leios_committee_size = 0;
        uint32_t leios_diffusion_period_length = 0;
        rational_u64 leios_quorum_stake_threshold {};
        uint32_t leios_vote_period_length = 0;
        ex_units max_endorser_block_execution_units {};
        uint32_t max_endorser_block_references_size = 0;
        uint32_t max_endorser_block_txs_size = 0;
        std::optional<rational_u64> max_pledge_leverage {};
        uint32_t max_ref_script_size_per_block = 0;
        uint32_t max_ref_script_size_per_endorser_block = 0;
        uint32_t max_ref_script_size_per_tx = 0;
        rational_u64 min_pool_margin {};
        uint32_t ref_script_cost_stride = 0;
        rational_u64 ref_script_cost_multiplier {};
        uint32_t peras_min_candidate_block_age = 0;
        rational_u64 peras_healing_factor { 1, 1 };
        uint16_t peras_cert_boost = 0;
        uint16_t peras_target_committee_size = 0;
        nil_optional_t<uint32_t> peras_bootstrap_round {};
        rational_u64 peras_quorum_threshold_safety_margin { 0, 1 };
        uint64_t ref_inputs_cost_per_multi_asset_policy = 0;
        uint64_t ref_inputs_cost_per_datum_byte = 0;
        // Keep signed costs in their original order until V4 evaluator support lands.
        std::vector<int64_t> plutus_v4_cost_model {};
        // Dormant v13 inputs are retained without implementing their activation.
        json::object parameters {};
    };

    struct config {
    private:
        struct immutable;
        std::shared_ptr<const immutable> _immutable;

    public:
        // Read-only facade over the shared immutable payload. Keeping these as
        // references preserves the existing field-style API without copying
        // genesis-derived containers into each runtime config.
        const block_hash &byron_genesis_hash;
        const uint32_t &byron_protocol_magic;
        const uint64_t &byron_start_time;
        const uint64_t &byron_epoch_length;
        const uint64_t &byron_slot_duration;
        const txo_map &byron_utxos;
        const byron_delegate_map &byron_delegates;
        const vkey_set &byron_issuers;
        const signer_set &byron_delegate_hashes;
        const uint64_t &byron_slots_per_chunk;
        const block_hash &shelley_genesis_hash;
        const uint64_t &shelley_epoch_length;
        const uint64_t &shelley_update_quorum;
        const uint64_t &shelley_max_lovelace_supply;
        const uint8_t &shelley_network_id;
        const double &shelley_active_slots;
        const rational_u64 &shelley_active_slots_coeff;
        const uint64_t &shelley_slots_per_kes_period;
        const uint64_t &shelley_max_kes_evolutions;
        const uint64_t &shelley_security_param;
        const uint64_t &shelley_epoch_blocks;
        const uint64_t &shelley_rewards_ready_slot;
        const uint64_t &shelley_stability_window;
        const uint64_t &shelley_randomness_stabilization_window;
        const uint64_t &shelley_voting_deadline;
        const uint64_t &shelley_chunks_per_epoch;
        const shelley_delegate_map &shelley_delegates;
        const shelley_initial_state_t &shelley_initial_state;
        const block_hash &alonzo_genesis_hash;
        const block_hash &conway_genesis_hash;
        const std::optional<dijkstra_genesis_t> &dijkstra_genesis;
        const plutus_cost_models &plutus_all_cost_models;
        const pool_voting_thresholds_t &conway_pool_voting_thresholds;
        const drep_voting_thresholds_t &conway_drep_voting_thresholds;
        const protocol_params &shelley_protocol_params;
        const protocol_params &alonzo_protocol_params;
        const protocol_params &conway_protocol_params;
        // Protocol version after configured epoch-zero hard forks.
        const protocol_version &initial_protocol_version;
        const committee_member_map &conway_committee_members;
        const rational_u64 &conway_committee_threshold;
        const constitution_t &conway_constitution;

        static const config &get();
        explicit config(const configs &cfg=configs_dir::get());
        config(const config &);
        config(config &&) noexcept;
        config &operator=(const config &) =delete;
        config &operator=(config &&) noexcept;

        bool shelley_start_known() const
        {
            return static_cast<bool>(_mutable.shelley_start_slot);
        }

        // Explicitly overrides the network default until the next reset.
        void set_shelley_start_epoch(uint64_t epoch) const;
        void reset_shelley_start_epoch() const;

        uint64_t shelley_start_slot() const
        {
            if (!_mutable.shelley_start_slot) [[unlikely]]
                throw error{"Shelley start is unknown"};
            return *_mutable.shelley_start_slot;
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
        struct mutable_data {
            std::optional<uint64_t> shelley_start_slot {};
        };
        mutable mutable_data _mutable {};

        config(std::shared_ptr<const immutable>, std::optional<uint64_t> shelley_start_slot) noexcept;

        static plutus_cost_models _prep_plutus_cost_models(const turbo::config &genesis);
        static shelley_delegate_map _shelley_prep_delegates(const turbo::config &genesis);
        static txo_map _byron_prep_utxos(const turbo::config &genesis);
        static byron_delegate_map _byron_prep_delegates(const turbo::config &genesis);
        static block_hash _verify_hash_byron(const std::string_view &hash_hex, const turbo::config &genesis);
        static block_hash _verify_hash(const std::string_view &hash_hex, const turbo::config &genesis);
    };
}
