#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/mutex.hpp>
#include <turbo/cardano/ledger/shelley.hpp>
#include <turbo/cardano/ledger/subchain.hpp>
#include <turbo/cardano/ledger/updates.hpp>
#include <turbo/common/format.hpp>

namespace turbo {
    struct scheduler;
}

namespace turbo::cardano::ledger {
    struct state {
        explicit state(const cardano::config &cfg=cardano::config::get(), scheduler &sched=scheduler::get());

        bool operator==(const state &o) const;
        void clear();

        void load_zpp(const std::string &path);
        void save_zpp(const std::string &path, std::unique_ptr<subchain_list> tmp_sc={});
        cbor_encoder to_cbor(const point &tip, int prio=1000) const;
        point deserialize_node(buffer data);
        point load_node(const std::string &path);
        void save_node(const std::string &path, const point &tip, int prio=1000) const;

        void track_era(uint64_t era, uint64_t slot);
        void process_cert(const cert_t &, const cert_loc_t &);
        void process_updates(updates_t &&);
        void process_block_updates(block_update_list &&);
        void process_timed_update(update_effects_t &, index::timed_update::item &&);
        update_effects_t process_timed_updates(timed_update_list &&);
        void process_utxo_updates(utxo_update_list &&);
        void finish_update_processing(update_effects_t &&, bool run_pulser=true);

        optional_point add_subchain(subchain &&sc)
        {
            mutex::scoped_lock sc_lk { _subchains_mutex };
            _subchains.add(std::move(sc));
            return _subchains.max_valid_point();
        }

        optional_point mark_subchain_valid(const uint64_t chunk_offset, const size_t num_valid_blocks)
        {
            mutex::scoped_lock sc_lk { _subchains_mutex };
            return _subchains.report_valid_blocks(chunk_offset, num_valid_blocks);
        }

        uint64_t valid_end_offset() const
        {
            mutex::scoped_lock lk { _subchains_mutex };
            return std::min(_subchains.valid_size(), _state->_end_offset);
        }

        subchain_list set_subchains(subchain_list &&new_sc)
        {
            auto orig_sc = std::move(_subchains);
            _subchains = std::move(new_sc);
            return orig_sc;
        }

        bool truncate_subchains(const uint64_t max_end_offset)
        {
            mutex::scoped_lock sc_lk { _subchains_mutex };
            return _subchains.truncate(max_end_offset);
        }

        void merge_same_epoch_subchains()
        {
            mutex::scoped_lock sc_lk { _subchains_mutex };
            _subchains.merge_same_epoch(_cfg);
        }

        const shelley_delegate_map &shelley_delegs() const
        {
            return _state->shelley_delegs();
        }

        void proposal_vote(const uint64_t slot, const param_update_vote &vote)
        {
            _state->proposal_vote(slot, vote);
        }

        void propose_update(const uint64_t slot, const param_update_proposal &prop)
        {
            _state->propose_update(slot, prop);
        }

        void register_pool(const pool_reg_cert &reg)
        {
            _state->register_pool(reg);
        }

        void vrf_process_updates(const std::vector<index::vrf::item> &updates)
        {
            _vrf_state->process_updates(updates);
        }

        void process_block(const uint64_t end_offset, const uint64_t era, const uint64_t slot, const uint64_t fees)
        {
            track_era(era, slot);
            add_fees(fees);
            _state->process_block(end_offset, slot);
        }

        void reserves(const uint64_t r)
        {
            _state->reserves(r);
        }

        void start_epoch(std::optional<uint64_t> new_epoch={});

        // Accessors

        const shelley::vrf_state &vrf_state() const
        {
            return *_vrf_state;
        }

        const txo_map &utxos() const
        {
            return _state->_utxo;
        }

        const ptr_to_stake_map &pointers() const
        {
            return _state->_ptr_to_stake;
        }

        cardano::amount unspent_reward(const cardano::stake_ident &id) const
        {
            const auto acc_it = _state->_accounts.find(id);
            if (acc_it != _state->_accounts.end() && acc_it->second.ptr)
                return { acc_it->second.reward };
            return { 0 };
        }

        const era_list eras() const
        {
            era_list copy {};
            std::copy(_eras.begin(), _eras.end(), std::back_inserter(copy));
            return copy;
        }

        const operating_pool_map &pool_stake_dist() const
        {
            return _state->_operating_stake_dist;
        }

        const pool_info_map &pool_params_mark() const
        {
            return _state->_mark.pool_params;
        }

        const pool_info_map &pool_params() const
        {
            return _state->_active_pool_params;
        }

        const pool_info_map &pool_params_future() const
        {
            return _state->_future_pool_params;
        }

        void add_fees(const uint64_t amount)
        {
            _state->add_fees(amount);
        }

        uint64_t reserves() const
        {
            return _state->_reserves;
        }

        uint64_t treasury() const
        {
            return _state->_treasury;
        }

        const protocol_params &params() const
        {
            return _state->_params;
        }

        const protocol_params &prev_params() const
        {
            return _state->_params_prev;
        }

        cardano::slot last_slot() const
        {
            return cardano::slot::from_epoch(_state->_epoch, _state->_epoch_slot, _cfg);
        }

        uint64_t end_offset() const
        {
            return _state->_end_offset;
        }

        const std::set<pool_hash> &pbft_pools() const
        {
            return _state->_pbft_pools;
        }

        uint64_t utxo_balance() const
        {
            return _state->utxo_balance();
        }

        bool exportable() const
        {
            if (_state->_params.protocol_ver.major >= 2 && (_state->_rewards_ready || _state->_epoch_slot < _cfg.shelley_randomness_stabilization_window))
                return true;
            return false;
        }

        const stake_pointer_distribution stake_pointers() const
        {
            return _state->_stake_pointers;
        }

        uint64_t epoch() const
        {
            return _state->_epoch;
        }

        // used only by _process_certs_and_param_updates
        bool has_pool(const pool_hash &id) const
        {
            return _state->has_pool(id);
        }

        // used only by _process_certs_and_param_updates
        bool has_stake(const stake_ident &id) const
        {
            return _state->has_stake(id);
        }

        // used only by _process_certs_and_param_updates
        bool has_drep(const credential_t &id) const
        {
            return _state->has_drep(id);
        }
    private:
        // non-serializable members:
        const cardano::config &_cfg;
        scheduler &_sched;

        mutable mutex::unique_lock::mutex_type _subchains_mutex alignas(mutex::alignment) {};
        subchain_list _subchains{};
        /// implementation-specific fields
        era_list _eras {};
        std::unique_ptr<shelley::state> _state;
        std::unique_ptr<shelley::vrf_state> _vrf_state;

        void _deserialize_node_ledger_state(cbor::zero2::value &);
        point _deserialize_node_vrf_state(cbor::zero2::value &);
        void _serialize_node_state(cbor_encoder &ser, const point &tip) const;
        void _serialize_node_vrf_state(cbor_encoder &ser, const point &tip) const;
        void _transition_ledger_era(uint64_t from_era, uint64_t to_era);
        void _transition_vrf_era(uint64_t from_era, uint64_t to_era);
        void _transition_era(uint64_t from_era, uint64_t to_era);

        delegation_map_copy _filtered_delegs(const size_t idx) const
        {
            delegation_map_copy delegs {};
            for (const auto &[stake_id, acc]: _state->_accounts) {
                if (const auto &deleg = acc.deleg_copy(idx); deleg)
                    delegs.emplace_back(stake_id, *deleg);
            }
            return delegs;
        }

        stake_distribution _filtered_stake_dist(const size_t idx) const
        {
            stake_distribution sd {};
            for (const auto &[stake_id, acc]: _state->_accounts) {
                if (const auto stake = acc.stake_copy(idx); stake > 0 && acc.deleg_copy(idx))
                    sd.try_emplace(stake_id, stake);
            }
            return sd;
        }
    };
}

namespace fmt {
    template<>
    struct formatter<turbo::cardano::ledger::reward_type>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            switch (v) {
                case turbo::cardano::ledger::reward_type::leader:
                    return fmt::format_to(ctx.out(), "reward_type::leader");

                case turbo::cardano::ledger::reward_type::member:
                    return fmt::format_to(ctx.out(), "reward_type::member");

                default:
                    return fmt::format_to(ctx.out(), "reward_type::unsupported");
            }
        }
    };

    template<>
    struct formatter<turbo::cardano::ledger::reward_update>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "reward_update(type: {} pool_id: {} amount: {})", v.type, v.pool_id, v.amount);
        }
    };

    template<>
    struct formatter<turbo::cardano::ledger::operating_pool_info>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "rel_stake: {} vrf: {}", v.rel_stake, v.vrf_vkey);
        }
    };
}
