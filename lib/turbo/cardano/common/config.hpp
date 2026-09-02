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
        const block_hash &alonzo_genesis_hash;
        const block_hash &conway_genesis_hash;
        const plutus_cost_models &plutus_all_cost_models;
        const pool_voting_thresholds_t &conway_pool_voting_thresholds;
        const drep_voting_thresholds_t &conway_drep_voting_thresholds;
        const protocol_params &shelley_protocol_params;
        const protocol_params &alonzo_protocol_params;
        const protocol_params &conway_protocol_params;
        const committee_member_map &conway_committee_members;
        const rational_u64 &conway_committee_threshold;
        const constitution_t &conway_constitution;

        static const config &get();
        explicit config(const configs &cfg=configs_dir::get());
        config(const config &);
        config(config &&) noexcept;
        config &operator=(const config &) =delete;
        config &operator=(config &&) noexcept;

        bool shelley_started() const
        {
            return static_cast<bool>(_mutable.shelley_start_slot);
        }

        void shelley_start_epoch(std::optional<uint64_t> epoch) const;

        uint64_t shelley_start_slot() const
        {
            if (_mutable.shelley_start_slot) [[likely]]
                return *_mutable.shelley_start_slot;
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
