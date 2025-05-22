/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */
#ifndef DAEDALUS_TURBO_CARDANO_LEDGER_SHELLEY_HPP
#define DAEDALUS_TURBO_CARDANO_LEDGER_SHELLEY_HPP

#include <dt/cardano/common/common.hpp>
#include <dt/cardano/common/cert.hpp>
#include <dt/cardano/shelley/block.hpp>
#include <dt/cardano/ledger/types.hpp>
#include <dt/parallel/encoder.hpp>
#include <dt/index/vrf.hpp>

namespace daedalus_turbo::cardano::ledger::shelley {
    using namespace cardano::shelley;

    struct vrf_state {
        using pool_update_map = map_t<pool_hash, uint64_t>;

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self._nonce_epoch, self._nonce_evolving, self._nonce_candidate, self._lab_prev_hash,
                self._prev_epoch_lab_prev_hash, self._slot_last, self._kes_counters);
        }

        vrf_state(const vrf_state &) =delete;
        vrf_state(vrf_state &&) =default;
        explicit vrf_state(const config &cfg=config::get());
        virtual ~vrf_state() =default;

        virtual void from_cbor(cbor::zero2::value &v);
        virtual void to_cbor(cbor_encoder &) const;
        virtual void from_zpp(parallel_decoder &);
        virtual void to_zpp(zpp_encoder &) const;

        void process_updates(const vector<index::vrf::item> &updates);
        void finish_epoch(const nonce &extra_entropy);

        bool operator==(const vrf_state &o) const
        {
            return _max_epoch_slot == o._max_epoch_slot && _nonce_epoch == o._nonce_epoch
                && _nonce_evolving == o._nonce_evolving && _nonce_candidate == o._nonce_candidate
                && _lab_prev_hash == o._lab_prev_hash && _prev_epoch_lab_prev_hash == o._prev_epoch_lab_prev_hash
                && _slot_last == o._slot_last && _kes_counters == o._kes_counters;
        }

        const pool_update_map &kes_counters() const
        {
            return _kes_counters;
        }

        const vrf_nonce &nonce_epoch() const
        {
            return _nonce_epoch;
        }

        const vrf_nonce &uc_leader() const
        {
            return _nonce_uc_leader;
        }

        const vrf_nonce &uc_nonce() const
        {
            return _nonce_uc_nonce;
        }

        uint64_t max_epoch_slot() const
        {
            return _max_epoch_slot;
        }

        void clear()
        {
            _nonce_epoch = _nonce_genesis;
            _nonce_evolving = _nonce_genesis;
            _nonce_candidate = _nonce_genesis;
            _lab_prev_hash = vrf_nonce {};
            _prev_epoch_lab_prev_hash.reset();
            _slot_last = 0;
            _kes_counters.clear();
        }
    protected:
        friend fmt::formatter<vrf_state>;
        const config &_cfg;
        const vrf_nonce _nonce_uc_nonce = vrf_nonce::from_hex("81e47a19e6b29b0a65b9591762ce5143ed30d0261e5d24a3201752506b20f15c");
        const vrf_nonce _nonce_uc_leader = vrf_nonce::from_hex("12dd0a6a7d0e222a97926da03adb5a7768d31cc7c5c2bd6828e14a7d25fa3a60");
        const vrf_nonce _nonce_genesis;
        vrf_nonce _nonce_epoch { _nonce_genesis };
        vrf_nonce _nonce_evolving { _nonce_genesis };
        vrf_nonce _nonce_candidate { _nonce_genesis };
        vrf_nonce _lab_prev_hash {};
        cardano::nonce _prev_epoch_lab_prev_hash {};
        uint64_t _slot_last = 0;
        pool_update_map _kes_counters {};
        uint64_t _max_epoch_slot;
    };

    struct state {
        state(const cardano::config &cfg, scheduler &sched);
        virtual ~state() =default;

        virtual point from_cbor(cbor::zero2::value &v);
        virtual void to_cbor(cbor_encoder &) const;
        virtual void from_zpp(parallel_decoder &);
        virtual void to_zpp(zpp_encoder &) const;

        virtual bool operator==(const state &o) const;
        virtual void clear();

        virtual const set<key_hash> &genesis_signers() const;

        virtual void run_pulser_if_ready();
        virtual void process_updates(updates_t &&);
        virtual void process_cert(const cert_t &, const cert_loc_t &);
        virtual void start_epoch(std::optional<uint64_t> new_epoch);

        virtual void register_pool(const pool_reg_cert &reg);
        virtual void retire_pool(const pool_hash &pool_id, uint64_t epoch);

        virtual bool has_pool(const pool_hash &id) const;
        virtual bool has_stake(const stake_ident &id) const;
        virtual bool has_drep(const credential_t &id) const;

        virtual void instant_reward_reserves(const stake_ident &stake_id, uint64_t reward);
        virtual void instant_reward_treasury(const stake_ident &stake_id, uint64_t reward);

        virtual const tx_out_data *utxo_find(const tx_out_ref &txo_id);
        virtual void utxo_add(const tx_out_ref &txo_id, tx_out_data &&txo_data);
        virtual void utxo_del(const tx_out_ref &txo_id);
        virtual uint64_t utxo_balance() const;

        virtual void withdraw_reward(const stake_ident &stake_id, uint64_t amount);
        virtual void update_stake(const stake_ident &stake_id, int64_t delta);
        virtual void update_pointer(const stake_pointer &ptr, int64_t delta);
        virtual void update_stake_id_hybrid(const stake_ident_hybrid &stake_id, int64_t delta);

        virtual void proposal_vote(uint64_t slot, const param_update_vote &vote);
        virtual void propose_update(uint64_t slot, const param_update_proposal &prop);

        virtual void process_block(uint64_t end_offset, uint64_t slot);
        virtual void add_pool_blocks(const pool_hash &pool_id, uint64_t num_blocks);
        virtual void sub_fees(uint64_t refund);
        virtual void add_fees(uint64_t amount);

        virtual void reserves(uint64_t new_reserves);
        virtual void treasury(uint64_t new_treasury);

        virtual const shelley_delegate_map &shelley_delegs() const;
        virtual void genesis_deleg_update(const key_hash &hash, const pool_hash &pool_id, const vrf_vkey &vrf_vkey);
        virtual void rotate_snapshots();

        virtual void process_cert(const stake_reg_cert &, const cert_loc_t &);
        virtual void process_cert(const stake_dereg_cert &, const cert_loc_t &);
        virtual void process_cert(const stake_deleg_cert &, const cert_loc_t &);
        virtual void process_cert(const pool_reg_cert &, const cert_loc_t &);
        virtual void process_cert(const pool_retire_cert &, const cert_loc_t &);
        virtual void process_cert(const genesis_deleg_cert &, const cert_loc_t &);
        virtual void process_cert(const instant_reward_cert &, const cert_loc_t &);

        virtual const protocol_params &params() const;
    protected:
        using encode_cbor_func = std::function<void(era_encoder &)>;

        friend ledger::state;
        const cardano::config &_cfg;
        scheduler &_sched;
        uint64_t _end_offset = 0;
        uint64_t _epoch_slot = 0;
        // serializable members
        uint64_t _pulsing_snapshot_slot = 0;
        reward_distribution_copy _reward_pulsing_snapshot {};
        pool_stake_distribution _active_pool_dist {};
        inv_delegation_map _active_inv_delegs {};

        /// fields that correspond to Cardano Node's binary state
        partitioned_map<stake_ident, account_info> _accounts {};

        uint64_t _epoch = 0;
        pool_block_dist _blocks_current {};
        pool_block_dist _blocks_before {};

        //// stateBefore.esAccountState
        uint64_t _reserves = 0;
        uint64_t _treasury = 0;

        //// stateBefore.esSnapshots
        ledger_copy _mark {}, _set {}, _go {};
        uint64_t _fees_next_reward = 0;

        //// stateBefore.utxoState
        txo_map _utxo {};
        uint64_t _deposited = 0;
        uint64_t _delta_fees = 0;
        uint64_t _fees_utxo = 0;
        map<pool_hash, param_update> _ppups {};
        map<pool_hash, param_update> _ppups_future {};

        // stateBefore.esLState.delegationState
        ptr_to_stake_map _ptr_to_stake {};
        shelley_delegate_map _future_shelley_delegs {};
        shelley_delegate_map _shelley_delegs { _cfg.shelley_delegates };
        stake_pointer_distribution _stake_pointers {};

        // stateBefore.esLState.delegationState.irwd
        stake_distribution _instant_rewards_reserves {};
        stake_distribution _instant_rewards_treasury {};

        // stateBefore.esLState.pstate
        pool_info_map _active_pool_params {};
        pool_info_map _future_pool_params {};
        pool_retiring_map _pools_retiring {};
        pool_deposit_map _pool_deposits {};

        // protocol params
        protocol_params _params = _default_params(_cfg);
        protocol_params _params_prev = _default_params(_cfg);
        nonmyopic_likelihood_map _nonmyopic {};
        uint64_t _nonmyopic_reward_pot = 0;

        // possibleRewardUpdate.rs
        uint64_t _delta_treasury = 0;
        uint64_t _delta_reserves = 0;
        uint64_t _reward_pot = 0;
        partitioned_reward_update_dist _potential_rewards {};
        bool _rewards_ready = false;
        nonmyopic_likelihood_map _nonmyopic_next {};

        // stakeDistrib
        operating_pool_map _operating_stake_dist {};
        uint64_t _blocks_past_voting_deadline = 0;

        // non-serializable cache entries
        mutable set<pool_hash> _pbft_pools = _make_pbft_pools(_shelley_delegs);

        template<std::integral T>
        static size_t _param_to_cbor(era_encoder &enc, const size_t idx, const std::optional<T> &val)
        {
            if (val) {
                enc.uint(idx);
                enc.uint(numeric_cast<uint64_t>(*val));
                return 1;
            }
            return 0;
        }

        static uint8_vector _parse_address(buffer buf);
        static size_t _param_to_cbor(era_encoder &enc, size_t idx, const std::optional<rational_u64> &val);
        static size_t _param_update_common_to_cbor(era_encoder &enc, const param_update &upd);

        // previously public methods
        virtual void register_stake(uint64_t slot, const stake_ident &stake_id, std::optional<uint64_t> deposit, size_t tx_idx=0, size_t cert_idx=0);
        virtual void retire_stake(uint64_t slot, const stake_ident &stake_id, std::optional<uint64_t> deposit);
        virtual void delegate_stake(const stake_ident &stake_id, const pool_hash &pool_id);

        void _apply_shelley_params(protocol_params &p) const;
        virtual void _add_encode_task(cbor_encoder &, const encode_cbor_func &) const;
        virtual void _apply_param_update(const param_update &update);
        virtual std::optional<param_update> _prep_param_update() const;
        virtual void _parse_protocol_params(protocol_params &params, cbor::zero2::value &values) const;
        virtual void _compute_rewards();

        virtual void _account_to_cbor(const account_info &acc, era_encoder &enc) const;
        virtual void _donations_to_cbor(era_encoder &) const;
        virtual void _delegation_gov_to_cbor(era_encoder &enc) const;
        virtual void _param_update_to_cbor(era_encoder &enc, const param_update &update) const;
        virtual void _params_to_cbor(era_encoder &enc, const protocol_params &params) const;
        virtual void _protocol_state_to_cbor(era_encoder &) const;
        virtual void _stake_pointer_stake_to_cbor(era_encoder &) const;
        virtual void _stake_pointers_to_cbor(era_encoder &) const;
        virtual void _process_block_updates(block_update_list &&);
        virtual void _process_timed_update(tx_out_ref_list &, timed_update_t &&);
        virtual tx_out_ref_list _process_timed_updates(timed_update_list &&);
        virtual void _process_utxo_updates(utxo_update_list &&);
        virtual void _process_collateral_use(tx_out_ref_list &&);
        uint64_t _transfer_instant_rewards(stake_distribution &rewards);
        virtual void _tick(uint64_t slot);

        virtual void _decode_possible_update(cbor::zero2::value &);
        virtual void _decode_accounts(cbor::zero2::value &);
        virtual void _decode_lstate(cbor::zero2::value &);
        virtual void _decode_snapshots(cbor::zero2::value &);
        virtual void _decode_likelihoods(cbor::zero2::value &);
        virtual void _decode_state_before(cbor::zero2::value &);
        virtual void _decode_snapshot(cbor::zero2::value &);
    private:
        static protocol_params _default_params(const cardano::config &cfg);
        static set<pool_hash> _make_pbft_pools(const shelley_delegate_map &delegs);

        template<typename VISITOR>
        void _visit(const VISITOR &v) const;

        void _node_load_delegation_state(cbor::zero2::value &);
        void _node_load_utxo_state(cbor::zero2::value &);
        void _node_load_vrf_state(cbor::zero2::value &);

        void _node_save_params(era_encoder &enc, const protocol_params &params) const;
        void _node_save_params_shelley(era_encoder &enc, const protocol_params &params) const;
        void _node_save_params_alonzo(era_encoder &enc, const protocol_params &params) const;
        void _node_save_params_babbage(era_encoder &enc, const protocol_params &params) const;
        void _node_save_eras(cbor_encoder &ser, const point &tip) const;
        void _node_save_ledger(cbor_encoder &ser) const;
        void _node_save_ledger_delegation(cbor_encoder &ser) const;
        void _node_save_ledger_utxo(cbor_encoder &ser) const;
        void _node_save_state(cbor_encoder &ser) const;
        void _node_save_snapshots(cbor_encoder &ser) const;
        void _node_save_state_before(cbor_encoder &ser) const;
        void _node_save_vrf_state(cbor_encoder &ser, const point &) const;

        uint64_t _retire_avvm_balance();
        std::optional<stake_ident> _extract_stake_id(const address &addr) const;
        void _prep_op_stake_dist();
        void _apply_future_pool_params();
        void _recompute_caches() const;
        uint64_t _total_stake(uint64_t reserves) const;
        void _rewards_prepare_pool_params(uint64_t &total, uint64_t &filtered, double z0,
            uint64_t staking_reward_pot, uint64_t total_stake, const pool_hash &pool_id, pool_info &info, uint64_t pool_blocks);
        std::pair<uint64_t, uint64_t> _rewards_prepare_pools(const pool_block_dist &pools_active, uint64_t staking_reward_pot, uint64_t total_stake);
        std::pair<uint64_t, uint64_t> _rewards_compute_part(size_t part_idx);
        uint64_t _compute_pool_rewards_parallel(const pool_block_dist &pools_active, uint64_t staking_reward_pot, uint64_t total_stake);
        void _clean_old_epoch_data();
        protocol_params _apply_param_updates();
        void _transfer_potential_rewards(const protocol_params &params_prev);
        std::pair<uint64_t, uint64_t> _retire_pools();
    };
}

namespace fmt {
    template<>
    struct formatter<daedalus_turbo::cardano::ledger::shelley::vrf_state>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "nonce_epoch: {} nonce_evolving: {} nonce_candidate: {} lab_prev_hash: {} prev_epoch_lab_prev_hash: {} slot_last: {}",
                v._nonce_epoch, v._nonce_evolving, v._nonce_candidate, v._lab_prev_hash, v._prev_epoch_lab_prev_hash, v._slot_last);
        }
    };
}

#endif // !DAEDALUS_TURBO_CARDANO_LEDGER_SHELLEY_HPP