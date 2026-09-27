/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/ledger/rules/ledger/transaction.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>

#include <turbo/cardano/ledger/shelley.hpp>
#include <turbo/cardano/ledger/cbor/decode/transaction-input.hpp>
#include <turbo/cardano/ledger/updates.hpp>
#include <turbo/cardano/shelley/block.hpp>
#include <turbo/common/timer.hpp>
#include <turbo/common/scope-exit.hpp>
#include <turbo/cbor/zero2.hpp>
#include <turbo/math/big-int.hpp>
#include <turbo/zpp.hpp>

namespace turbo::cardano::ledger::shelley {
    template<typename T>
    concept Clearable = requires(T a)
    {
        a.clear();
    };

    template<typename T>
    concept Sizable = requires(T a)
    {
        a.size();
    };

    template<typename M>
    void clear_partitions(M &map, scheduler &sched, const std::string &task_group) noexcept
    {
        if (sched.num_workers() < 4) {
            map.clear();
            return;
        }
        try {
            sched.wait_all(task_group, [&](const auto &, const auto &submit_f) {
                for (size_t part_idx = 0; part_idx < M::num_parts; ++part_idx) {
                    submit_f({ 1000, task_group, [&map, part_idx] {
                        map.clear_partition(part_idx);
                    }});
                }
            });
        } catch (const std::exception &ex) {
            logger::warn("parallel clear {} failed, finishing sequentially: {}", task_group, ex.what());
            map.clear();
        } catch (...) {
            logger::warn("parallel clear {} failed with an unknown exception, finishing sequentially", task_group);
            map.clear();
        }
    }

    vrf_state::vrf_state(const config &cfg):
        _cfg { cfg },
        _nonce_genesis { _cfg.shelley_genesis_hash },
        _max_epoch_slot { _cfg.shelley_epoch_length - _cfg.shelley_stability_window }
    {
        logger::debug("sheley::vrf_state created nonce_genesis: {} max_epoch_slot: {}", _nonce_genesis, _max_epoch_slot);
    }

    void vrf_state::from_cbor(cbor::zero2::value &v)
    {
        decode_versioned(v, [&](auto &dv1) {
            auto &it = dv1.array();
            _slot_last = decode_versioned(it.read(), [](auto &dv) {
                return dv.uint();
            });
            {
                auto &state = it.read();
                auto &state_it = state.array();
                {
                    auto &part1 = state_it.read();
                    auto &p1_it = part1.array();
                    _kes_counters = decltype(_kes_counters)::from_cbor(p1_it.read());
                    _nonce_evolving = decode_versioned(p1_it.read(), [](auto &dv) {
                        return dv.bytes();
                    });
                    _nonce_candidate = decode_versioned(p1_it.read(), [](auto &dv) {
                        return dv.bytes();
                    });
                }
                {
                    auto &part2 = state_it.read();
                    auto &p2_it = part2.array();
                    _nonce_epoch = decode_versioned(p2_it.read(), [](auto &dv) {
                        return dv.bytes();
                    });
                    _prev_epoch_lab_prev_hash = decltype(_prev_epoch_lab_prev_hash)::from_cbor(p2_it.read());
                }
                _lab_prev_hash = decode_versioned(state_it.read(), [](auto &dv) {
                    return dv.bytes();
                });
            }
        });
    }

    void vrf_state::from_zpp(parallel_decoder &dec)
    {
        dec.add([&](const auto b) {
            zpp::deserialize(*this, b);
        });
    }

    void vrf_state::to_zpp(zpp_encoder &ser) const
    {
        ser.add([&](auto) {
            return zpp::serialize(*this);
        });
    }

    void vrf_state::process_updates(const std::vector<index::vrf::item> &updates)
    {
        crypto::blake2b::hash_32 nonce_block {};
        for (const auto &item: updates) {
            if (item.slot < _slot_last) [[unlikely]]
                throw error(fmt::format("got block with a slot number {} when last seed slot is : {}", item.slot, _slot_last));
            if (item.era < 6) {
                crypto::blake2b::digest(nonce_block, item.nonce_result);
            } else {
                nonce_block = vrf_nonce_value(item.leader_result);
            }
            //logger::debug("VRF update slot: {} prev_evolving_nonce: {} prev_candidate_nonce: {} epoch_nonce: {} prev_lab_nonce: {}",
            //    item.slot, _nonce_evolving, _nonce_candidate, _nonce_epoch, _lab_prev_hash);
            const auto item_slot = cardano::slot { item.slot, _cfg };
            _nonce_evolving = vrf_nonce_accumulate(_nonce_evolving, nonce_block);
            if (item_slot.epoch_slot() < _max_epoch_slot)
                _nonce_candidate = _nonce_evolving;
            _lab_prev_hash = item.prev_hash;
            _slot_last = item.slot;
            //logger::debug("VRF update slot: {} eta: {} new nonce_evolving_nonce: {} new_lab_nonce: {} nonce_candidate: {}", item_slot, nonce_block, _nonce_evolving, _lab_prev_hash, _nonce_candidate);
            auto [kes_it, kes_created] = _kes_counters.try_emplace(item.pool_id, item.kes_counter);
            if (!kes_created) {
                if (item.kes_counter > kes_it->second)
                    kes_it->second = item.kes_counter;
                else if (item.kes_counter < kes_it->second) [[unlikely]]
                    throw error(fmt::format("slot: {} out of order KES counter {} < {} for pool: {}", item_slot, item.kes_counter, kes_it->second, item.pool_id));
            }
        }
    }

    void vrf_state::finish_epoch(const nonce &extra_entropy)
    {
        //logger::debug("vrf::state::finish_epoch: {}", extra_entropy);
        const auto prev_epoch_nonce = _nonce_epoch;
        if (_prev_epoch_lab_prev_hash) {
            if (extra_entropy) {
                _nonce_epoch = vrf_nonce_accumulate(vrf_nonce_accumulate(_nonce_candidate, *_prev_epoch_lab_prev_hash), extra_entropy.value());
            } else {
                _nonce_epoch = vrf_nonce_accumulate(_nonce_candidate, *_prev_epoch_lab_prev_hash);
            }
        } else {
            _nonce_epoch = _nonce_candidate;
        }
        logger::debug("VRF finish_epoch last_slot: {} prev nonce_epoch: {} new nonce_epoch: {} nonce_evolving: {} prev_lab_prev_hash: {} new prev_lab_prev_hash: {} extra_entropy: {}",
            _slot_last, prev_epoch_nonce, _nonce_epoch, _nonce_evolving, _prev_epoch_lab_prev_hash, _lab_prev_hash, extra_entropy);
        _nonce_candidate = _nonce_evolving;
        _prev_epoch_lab_prev_hash = _lab_prev_hash;
    }

    state::state(const cardano::config &cfg, scheduler &sched, const state_init_mode mode):
        _cfg { cfg }, _sched { sched }
    {
        _reset_shelley_delegs_schedule();
        if (mode == state_init_mode::genesis)
            _utxo = _cfg.byron_utxos;
    }

    state::~state()
    {
        if (!_utxo.empty())
            clear_partitions(_utxo, _sched, "shelley-state:destroy-utxo");
        if (!_accounts.empty())
            clear_partitions(_accounts, _sched, "shelley-state:destroy-accounts");
    }

    void state::_add_encode_task(cbor_encoder &ser, const encode_cbor_func &t) const
    {
        ser.add([t](auto enc) {
            t(enc);
            return std::move(enc.cbor());
        });
    }

    void state::_decode_accounts(cbor::zero2::value &v)
    {
        auto &it = v.array();
        _treasury = it.read().uint();
        _reserves = it.read().uint();
    }

    void state::_decode_lstate(cbor::zero2::value &v)
    {
        auto &it = v.array();
        const auto ambiguous_pstate = _node_load_delegation_state(it.read());
        _node_load_utxo_state(it.read());
        if (ambiguous_pstate) {
            if (_params.protocol_ver.major >= 11)
                _pool_deposits.clear();
            else
                _pools_retiring.clear();
        }
        if (_params.protocol_ver.major >= 11) {
            // Protocol-11 StakePoolState records the active delegators, but
            // their UTxO stake is decoded only by _node_load_utxo_state.
            // Rebuild this derived distribution once both inputs are owned.
            for (const auto &[pool_id, delegators]: _active_inv_delegs) {
                for (const auto &stake_id: delegators) {
                    const auto acc_it = _accounts.find(stake_id);
                    if (acc_it == _accounts.end()) [[unlikely]]
                        throw error(fmt::format("pool {} references unknown delegator {}", pool_id, stake_id));
                    _active_pool_dist.add(pool_id, acc_it->second.stake + acc_it->second.reward);
                }
            }
        }
    }

    void state::_decode_snapshots(cbor::zero2::value &v)
    {
        auto &it = v.array();
        struct snapshot_copy {
            size_t idx;
            ledger_copy &dst_copy;
        };
        for (const auto &[idx, dst]: { snapshot_copy { 0, _mark }, snapshot_copy { 1, _set }, snapshot_copy { 2, _go } }) {
            auto &snap = it.read();
            auto &snap_it = snap.array();
            {
                auto &stake_v = snap_it.read();
                auto &stake_it = stake_v.map();
                while (!stake_it.done()) {
                    auto &k = stake_it.read_key();
                    const auto stake_id = stake_ident::from_cbor(k);
                    auto &acc =_accounts[stake_id];
                    acc.stake_copy(idx) = stake_it.read_val(std::move(k)).uint();
                }
            }
            {
                auto &deleg_v = snap_it.read();
                auto &deleg_it = deleg_v.map();
                while (!deleg_it.done()) {
                    auto &k = deleg_it.read_key();
                    const auto stake_id = stake_ident::from_cbor(k);
                    auto &acc =_accounts[stake_id];
                    acc.deleg_copy(idx) = deleg_it.read_val(std::move(k)).bytes();
                }
            }
            {
                dst.pool_params = map_from_cbor<decltype(dst.pool_params)>(snap_it.read());
                /*auto &pool_v = snap_it.read();
                auto &pool_it = pool_v.map();
                while (!pool_it.done()) {
                    auto &k = pool_it.read_key();
                    const auto pool_id = k.bytes();
                    auto &v = pool_it.read_val(std::move(k));
                    dst.pool_params.try_emplace(pool_id, pool_info { cardano::pool_params::from_cbor(v.array()) });
                }*/
            }
        }
        //_fees_next_reward = snapshots.at(3).uint();
    }

    void state::_decode_likelihoods(cbor::zero2::value &v)
    {
        auto &it = v.array();
        _nonmyopic = decltype(_nonmyopic)::from_cbor(it.read());
        _nonmyopic_reward_pot = it.read().uint();
    }

    void state::_decode_state_before(cbor::zero2::value &v)
    {
        auto &it = v.array();
        _decode_accounts(it.read());
        _decode_lstate(it.read());
        _decode_snapshots(it.read());
        _decode_likelihoods(it.read());
    }

    void state::_decode_possible_update(cbor::zero2::value &v)
    {
        _rewards_ready = false;
        auto &v_it = v.array();
        if (!v_it.done()) {
            decode_versioned(v.at(0), [&](auto &dv) {
                auto &it = dv.array();
                _delta_treasury = it.read().uint();
                _delta_reserves = it.read().uint();
                _potential_rewards = map_from_cbor<decltype(_potential_rewards)>(it.read());
                _delta_fees = it.read().uint();
                {
                    auto &nm_v = it.read();
                    auto &nm_it = nm_v.array();
                    _nonmyopic_next = decltype(_nonmyopic_next)::from_cbor(nm_it.read());
                    _reward_pot = nm_it.read().uint();
                }
            });
            _rewards_ready = true;
        }
    }

    void state::_decode_snapshot(cbor::zero2::value &snap)
    {
        auto &it = snap.array();
        _epoch = it.read().uint();
        _blocks_before = map_from_cbor<decltype(_blocks_before)>(it.read());
        _blocks_current = map_from_cbor<decltype(_blocks_current)>(it.read());
        _decode_state_before(it.read());
        _decode_possible_update(it.read());
        _operating_stake_dist = decltype(_operating_stake_dist)::from_cbor(it.read());
    }

    void state::_decode_protocol_state(cbor::zero2::value &v)
    {
        auto &it = v.array();
        _ppups = map_from_cbor<decltype(_ppups)>(it.read());
        _ppups_future = map_from_cbor<decltype(_ppups_future)>(it.read());
        _parse_protocol_params(_params, it.read());
        _parse_protocol_params(_params_prev, it.read());
    }

    void state::_decode_donations(cbor::zero2::value &v)
    {
        static_cast<void>(v.uint());
    }

    point state::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        auto tip = point::from_ledger_cbor(it.read().array().read());
        _decode_snapshot(it.read());
        _blocks_past_voting_deadline = it.read().uint();
        _pulsing_snapshot_slot = slot::from_epoch(_epoch, _cfg) + _cfg.shelley_randomness_stabilization_window;
        _reward_pulsing_snapshot_ready = _rewards_ready || _epoch_slot > _cfg.shelley_randomness_stabilization_window;
        _recompute_caches();
        _reset_shelley_delegs_schedule(false);
        return tip;
    }

    bool state::operator==(const state &o) const
    {
        return typeid(*this) == typeid(o)
            && _end_offset == o._end_offset
            && _epoch_slot == o._epoch_slot
            && _pulsing_snapshot_slot == o._pulsing_snapshot_slot
            && _reward_pulsing_snapshot_ready == o._reward_pulsing_snapshot_ready
            && _reward_pulsing_snapshot == o._reward_pulsing_snapshot
            && _active_pool_dist == o._active_pool_dist
            && _active_inv_delegs == o._active_inv_delegs
            && _accounts == o._accounts

            && _epoch == o._epoch
            && _blocks_current == o._blocks_current
            && _blocks_before == o._blocks_before

            && _reserves == o._reserves
            && _treasury == o._treasury

            && _mark == o._mark
            && _set == o._set
            && _go == o._go
            && _fees_next_reward == o._fees_next_reward

            && _utxo == o._utxo
            && _deposited == o._deposited
            && _delta_fees == o._delta_fees
            && _fees_utxo == o._fees_utxo
            && _ppups == o._ppups
            && _ppups_future == o._ppups_future

            && _ptr_to_stake == o._ptr_to_stake
            && _future_shelley_delegs == o._future_shelley_delegs
            && _shelley_delegs == o._shelley_delegs
            && _stake_pointers == o._stake_pointers

            && _instant_rewards_reserves == o._instant_rewards_reserves
            && _instant_rewards_treasury == o._instant_rewards_treasury

            && _active_pool_params == o._active_pool_params
            && _future_pool_params == o._future_pool_params
            && _pools_retiring == o._pools_retiring
            && _pool_deposits == o._pool_deposits
            && _pool_vrf_key_hashes == o._pool_vrf_key_hashes

            && _params == o._params
            && _params_prev == o._params_prev
            && _nonmyopic == o._nonmyopic
            && _nonmyopic_reward_pot == o._nonmyopic_reward_pot

            && _delta_treasury == o._delta_treasury
            && _delta_reserves == o._delta_reserves
            && _reward_pot == o._reward_pot
            && _potential_rewards == o._potential_rewards
            && _rewards_ready == o._rewards_ready
            && _nonmyopic_next == o._nonmyopic_next

            && _operating_stake_dist == o._operating_stake_dist
            && _blocks_past_voting_deadline == o._blocks_past_voting_deadline;
    }

    const signer_set &state::genesis_signers() const
    {
        return _cfg.byron_delegate_hashes;
    }

#include <turbo/cardano/ledger/rules/pool/shelley.ipp>

    bool state::has_pool(const pool_hash &id) const
    {
        return _active_pool_params.contains(id);
    }

    bool state::has_stake(const stake_ident &id) const
    {
        const auto acc_it = _accounts.find(id);
        return acc_it != _accounts.end() && acc_it->second.ptr;
    }

    bool state::has_drep(const credential_t &) const
    {
        return false;
    }

#include <turbo/cardano/ledger/rules/compatibility/shelley.ipp>

#include <turbo/cardano/ledger/rules/utxos/stake-deltas.ipp>

    const tx_out_data *state::utxo_find(const tx_out_ref &txo_id)
    {
        if (const auto it = _utxo.find(txo_id);it != _utxo.end()) [[likely]]
            return &it->second;
        return nullptr;
    }

#include <turbo/cardano/ledger/rules/utxos/shelley.ipp>

#include <turbo/cardano/ledger/rules/block-body/shelley.ipp>

#include <turbo/cardano/ledger/rules/certs/shelley.ipp>

#include <turbo/cardano/ledger/rules/deleg/shelley.ipp>

#include <turbo/cardano/ledger/rules/ledger/shelley.ipp>

#include <turbo/cardano/ledger/rules/reward-update/shelley.ipp>

    uint64_t state::utxo_balance() const
    {
        std::atomic_uint64_t total_balance = 0;
        static const std::string task_group { "validator::state::utxo_balance" };
        _sched.wait_all(task_group, [&](const auto &todo, const auto &submit_f) {
            for (size_t pi = 0; pi < _utxo.num_parts; ++pi) {
                submit_f({ 1000, task_group, [&, pi, todo] {
                    uint64_t part_balance = 0;
                    const auto &part = _utxo.partition(pi);
                    for (const auto &[txo_id, txo_data]: part) {
                        part_balance += txo_data.coin;
                    }
                    total_balance.fetch_add(part_balance, std::memory_order_relaxed);
                }});
            }
        });
        return total_balance.load(std::memory_order_relaxed);
    }

    const shelley_delegate_map &state::shelley_delegs() const
    {
        return _shelley_delegs;
    }

    const shelley_delegate_schedule &state::shelley_delegs_schedule() const
    {
        return _shelley_delegs_schedule;
    }

#include <turbo/cardano/ledger/rules/snap/shelley.ipp>

#include <turbo/cardano/ledger/rules/epoch/shelley.ipp>

    void state::reserves(const uint64_t r)
    {
        logger::trace("epoch: {} override reserves with {} while {} currently, diff: {}",
            _epoch, r, _reserves, static_cast<int64_t>(_reserves) - static_cast<int64_t>(r));
        _reserves = r;
    }

    void state::treasury(uint64_t t)
    {
        logger::trace("epoch: {} override treasury with {} while {} currently, diff: {}",
            _epoch, t, _treasury, static_cast<int64_t>(_treasury) - static_cast<int64_t>(t));
        _treasury = t;
    }

    static void _apply_byron_params(cardano::protocol_params &p, const cardano::config &)
    {
        p.protocol_ver = { 0, 0 };
    }

    void state::_apply_shelley_params(protocol_params &p) const
    {
        const auto &initial = _cfg.shelley_protocol_params;
        p.min_fee_a = initial.min_fee_a;
        p.min_fee_b = initial.min_fee_b;
        p.max_block_body_size = initial.max_block_body_size;
        p.max_transaction_size = initial.max_transaction_size;
        p.max_block_header_size = initial.max_block_header_size;
        p.key_deposit = initial.key_deposit;
        p.pool_deposit = initial.pool_deposit;
        p.e_max = initial.e_max;
        p.n_opt = initial.n_opt;
        p.expansion_rate = initial.expansion_rate;
        p.treasury_growth_rate = initial.treasury_growth_rate;
        p.pool_pledge_influence = initial.pool_pledge_influence;
        p.decentralization = initial.decentralization;
        p.min_utxo_value = initial.min_utxo_value;
        p.min_pool_cost = initial.min_pool_cost;
    }

    protocol_params state::_default_params(const cardano::config &cfg)
    {
        protocol_params p {};
        _apply_byron_params(p, cfg);
        return p;
    }

    flat_set<pool_hash> state::_make_pbft_pools(const shelley_delegate_map &delegs)
    {
        flat_set<pool_hash> pools {};
        pools.reserve(delegs.size());
        for (const auto &[id, meta]: delegs)
            pools.emplace(meta.delegate);
        return pools;
    }

    /*uint8_vector state::_parse_address(const buffer buf)
    {
        address addr { buf };
        if (addr.bytes()[0] == 0x82)
            return cbor::parse(addr.bytes()).at(0).tag().second->buf();
        return buf;
    }*/

    void state::_parse_protocol_params(protocol_params &params, cbor::zero2::value &v) const
    {
        _apply_shelley_params(params);
        auto &it = v.array();
        params.min_fee_a = it.read().uint();
        params.min_fee_b = it.read().uint();
        params.max_block_body_size = it.read().uint();
        params.max_transaction_size = it.read().uint();
        params.max_block_header_size = it.read().uint();
        params.key_deposit = it.read().uint();
        params.pool_deposit = it.read().uint();
        params.e_max = it.read().uint();
        params.n_opt = it.read().uint();
        params.pool_pledge_influence = decltype(params.pool_pledge_influence)::from_cbor(it.read());
        params.expansion_rate = decltype(params.expansion_rate)::from_cbor(it.read());
        params.treasury_growth_rate = decltype(params.treasury_growth_rate)::from_cbor(it.read());
        params.decentralization = decltype(params.decentralization)::from_cbor(it.read());
        params.extra_entropy = decltype(params.extra_entropy)::from_cbor(it.read());
        params.protocol_ver.major = it.read().uint();
        params.protocol_ver.minor = it.read().uint();
        params.min_utxo_value = it.read().uint();
    }

    void state::_recompute_caches() const
    {
        _pbft_pools = _make_pbft_pools(_shelley_delegs);
    }

#include <turbo/cardano/ledger/rules/rewards/shelley.ipp>

#include <turbo/cardano/ledger/rules/tick/shelley.ipp>

#include <turbo/cardano/ledger/rules/pool-reap/shelley.ipp>

    bool state::_node_load_delegation_state(cbor::zero2::value &v)
    {
        enum class pstate_format {
            unknown,
            legacy,
            pv11
        };

        auto &it = v.array();
        it.skip(1);
        auto format = pstate_format::unknown;
        {
            auto &pstate_v = it.read();
            auto &pstate_it = pstate_v.array();

            _active_pool_params.clear();
            _future_pool_params.clear();
            _pools_retiring.clear();
            _pool_deposits.clear();
            _pool_vrf_key_hashes.clear();
            _active_inv_delegs.clear();
            _active_pool_dist.clear();

            const auto store_pool_state = [this](const pool_hash &pool_id, cbor::zero2::array_reader &state_it, const vrf_vkey &vrf) {
                pool_params params {};
                params.vrf_vkey = vrf;
                params.pledge = state_it.read().uint();
                params.cost = state_it.read().uint();
                params.margin = decltype(params.margin)::from_cbor(state_it.read());
                const auto reward_credential = stake_ident::from_cbor(state_it.read());
                params.reward_id.at(0) = (reward_credential.script ? 0xF0 : 0xE0) | _cfg.shelley_network_id;
                memcpy(params.reward_id.data() + 1, reward_credential.hash.data(), reward_credential.hash.size());
                params.owners = decltype(params.owners)::from_cbor(state_it.read());
                params.relays = decltype(params.relays)::from_cbor(state_it.read());
                params.metadata = decltype(params.metadata)::from_cbor(state_it.read());
                const auto deposit = state_it.read().uint();
                const auto delegators = set_t<stake_ident>::from_cbor(state_it.read());
                _active_pool_params.try_emplace(pool_id, std::move(params));
                _pool_deposits.try_emplace(pool_id, deposit);
                _active_pool_dist.create(pool_id);
                auto &inv_delegs = _active_inv_delegs[pool_id];
                inv_delegs.insert(delegators.begin(), delegators.end());
            };
            const auto load_pool_state = [&](cbor::zero2::map_reader &map_it) {
                auto &key = map_it.read_key();
                const pool_hash pool_id { key.bytes() };
                auto &value = map_it.read_val(std::move(key));
                auto &state_it = value.array();
                const vrf_vkey vrf { state_it.read().bytes() };
                store_pool_state(pool_id, state_it, vrf);
            };
            const auto load_pool_params = [](cbor::zero2::map_reader &map_it, pool_info_map &dst) {
                auto &key = map_it.read_key();
                const pool_hash pool_id { key.bytes() };
                auto &value = map_it.read_val(std::move(key));
                dst.try_emplace(pool_id, pool_info::from_cbor(value));
            };
            const auto load_pool_uint = [](cbor::zero2::map_reader &map_it, auto &dst) {
                auto &key = map_it.read_key();
                const pool_hash pool_id { key.bytes() };
                dst.try_emplace(pool_id, map_it.read_val(std::move(key)).uint());
            };

            // Legacy PState starts with active pool parameters (28-byte keys),
            // while PV11 starts with VRF-key occurrences (32-byte keys).
            {
                auto &map_it = pstate_it.read().map();
                if (!map_it.done()) {
                    auto &key = map_it.read_key();
                    const auto key_bytes = key.bytes();
                    if (key_bytes.size() == sizeof(vrf_vkey)) {
                        format = pstate_format::pv11;
                        const vrf_vkey vrf { key_bytes };
                        _pool_vrf_key_hashes.try_emplace(vrf, map_it.read_val(std::move(key)).uint());
                        while (!map_it.done()) {
                            auto &next_key = map_it.read_key();
                            const vrf_vkey next_vrf { next_key.bytes() };
                            _pool_vrf_key_hashes.try_emplace(next_vrf, map_it.read_val(std::move(next_key)).uint());
                        }
                    } else if (key_bytes.size() == sizeof(pool_hash)) {
                        format = pstate_format::legacy;
                        const pool_hash pool_id { key_bytes };
                        auto &value = map_it.read_val(std::move(key));
                        _active_pool_params.try_emplace(pool_id, pool_info::from_cbor(value));
                        while (!map_it.done())
                            load_pool_params(map_it, _active_pool_params);
                    } else {
                        throw error(fmt::format("unsupported PState key size: {}", key_bytes.size()));
                    }
                }
            }

            // If the first map was empty, the first field in a value from the
            // second map discriminates PoolParams (pool id) from StakePoolState (VRF).
            {
                auto &map_it = pstate_it.read().map();
                if (format == pstate_format::pv11) {
                    while (!map_it.done())
                        load_pool_state(map_it);
                } else if (format == pstate_format::legacy) {
                    while (!map_it.done())
                        load_pool_params(map_it, _future_pool_params);
                } else if (!map_it.done()) {
                    auto &key = map_it.read_key();
                    const pool_hash pool_id { key.bytes() };
                    auto &value = map_it.read_val(std::move(key));
                    auto &value_it = value.array();
                    const auto first_field = value_it.read().bytes();
                    if (first_field.size() == sizeof(vrf_vkey)) {
                        format = pstate_format::pv11;
                        store_pool_state(pool_id, value_it, vrf_vkey { first_field });
                        while (!map_it.done())
                            load_pool_state(map_it);
                    } else if (first_field.size() == sizeof(pool_hash)) {
                        format = pstate_format::legacy;
                        _future_pool_params.try_emplace(pool_id, pool_info { pool_params::from_cbor(value_it) });
                        while (!map_it.done())
                            load_pool_params(map_it, _future_pool_params);
                    } else {
                        throw error(fmt::format("unsupported PState value discriminator size: {}", first_field.size()));
                    }
                }
            }

            // With two empty leading maps, field three is either PoolParams
            // (PV11) or an epoch number (legacy), so its value type is sufficient.
            {
                auto &map_it = pstate_it.read().map();
                if (format == pstate_format::pv11) {
                    while (!map_it.done())
                        load_pool_params(map_it, _future_pool_params);
                } else if (format == pstate_format::legacy) {
                    while (!map_it.done())
                        load_pool_uint(map_it, _pools_retiring);
                } else if (!map_it.done()) {
                    auto &key = map_it.read_key();
                    const pool_hash pool_id { key.bytes() };
                    auto &value = map_it.read_val(std::move(key));
                    if (value.type() == cbor::major_type::array) {
                        format = pstate_format::pv11;
                        _future_pool_params.try_emplace(pool_id, pool_info::from_cbor(value));
                        while (!map_it.done())
                            load_pool_params(map_it, _future_pool_params);
                    } else if (value.type() == cbor::major_type::uint) {
                        format = pstate_format::legacy;
                        _pools_retiring.try_emplace(pool_id, value.uint());
                        while (!map_it.done())
                            load_pool_uint(map_it, _pools_retiring);
                    } else {
                        throw error(fmt::format("unsupported PState third-field value type: {}", value.type()));
                    }
                }
            }

            {
                auto &fourth = pstate_it.read();
                if (format == pstate_format::pv11) {
                    _pools_retiring = map_from_cbor<decltype(_pools_retiring)>(fourth);
                } else if (format == pstate_format::legacy) {
                    _pool_deposits = map_from_cbor<decltype(_pool_deposits)>(fourth);
                } else {
                    // All preceding maps are empty. Preserve the only ambiguous
                    // field as owned values until protocol parameters are decoded.
                    _pool_deposits = map_from_cbor<decltype(_pool_deposits)>(fourth);
                    _pools_retiring = _pool_deposits;
                }
            }
        }
        {
            auto &dstate_v = it.read();
            auto &dstate_it = dstate_v.array();
            // #0 - reward accounts and pointers - contains a reverse map already read
            {
                // #0 - reward accounts
                _accounts = map_from_cbor<decltype(_accounts)>(dstate_it.read().array().read());
                _ptr_to_stake.clear();
                for (const auto &[stake_id, acc]: _accounts) {
                    _ptr_to_stake[acc.ptr.value()] = stake_id;
                }
                /*auto &stake_it = dstate_it.read().array().read().map();
                while (!stake_it.done()) {
                    auto &key = stake_it.read_key();
                    const auto stake_id = stake_ident::from_cbor(key);
                    auto &acc = _accounts[stake_id];
                    auto &val = stake_it.read_val(std::move(key));
                    auto &v_it = val.array();
                    {
                        auto &cred = v_it.read();
                        auto &c_it = cred.array();
                        auto &cred2 = c_it.read();
                        auto &c2_it = cred2.array();
                        auto &cred3 = c2_it.read();
                        auto &c3_it = cred3.array();
                        acc.reward = c3_it.read().uint();
                        acc.deposit = c3_it.read().uint();
                    }
                    const auto stake_ptr = stake_pointer::from_cbor(v_it.read());
                    acc.ptr = stake_ptr;
                    _ptr_to_stake.try_emplace(stake_ptr, stake_id);
                    _accounts[stake_id].deleg = decltype(_accounts[stake_id].deleg)::from_cbor(v_it.read());
                }*/
                // #1 - pointer accounts - redundant, ignoring
            }
            // #1
            _future_shelley_delegs = map_from_cbor<decltype(_future_shelley_delegs)>(dstate_it.read());
            // #2
            _shelley_delegs = map_from_cbor<decltype(_shelley_delegs)>(dstate_it.read());
            // #3 irwd
            {
                auto &rewards_v = dstate_it.read();
                auto &r_it = rewards_v.array();
                _instant_rewards_reserves = map_from_cbor<decltype(_instant_rewards_reserves)>(r_it.read());
                _instant_rewards_treasury = map_from_cbor<decltype(_instant_rewards_treasury)>(r_it.read());
            }

        }
        return format == pstate_format::unknown;
    }

    void state::_node_load_utxo_state(cbor::zero2::value &v)
    {
        auto &it = v.array();
        _utxo.clear();
        _utxo = map_from_cbor<decltype(_utxo)>(it.read());
        _deposited = it.read().uint();
        _fees_utxo = it.read().uint();
        _decode_protocol_state(it.read());
        {
            auto &acc_v = it.read();
            auto &acc_it = acc_v.array();
            {
                auto &stake_v = acc_it.read();
                auto &stake_it = stake_v.map();
                while (!stake_it.done()) {
                    auto &key = stake_it.read_key();
                    const auto stake_id = stake_ident::from_cbor(key);
                    _accounts[stake_id].stake = stake_it.read_val(std::move(key)).uint();
                }
            }
            _stake_pointers = map_from_cbor<decltype(_stake_pointers)>(acc_it.read());
        }
        if (!it.done())
            _decode_donations(it.read());
    }

    template<typename VISITOR>
    void state::_visit(const VISITOR &v) const
    {
        v(_end_offset);
        v(_epoch_slot);

        v(_pulsing_snapshot_slot);
        v(_reward_pulsing_snapshot_ready);
        v(_reward_pulsing_snapshot);
        v(_active_pool_dist);
        v(_active_inv_delegs);

        v(_accounts);

        v(_epoch);
        v(_blocks_current);
        v(_blocks_before);

        v(_reserves);
        v(_treasury);

        v(_mark);
        v(_set);
        v(_go);
        v(_fees_next_reward);

        for (size_t pi = 0; pi < _utxo.num_parts; ++pi)
            v(_utxo.partition(pi));

        v(_deposited);
        v(_delta_fees);
        v(_fees_utxo);
        v(_ppups);
        v(_ppups_future);

        v(_ptr_to_stake);
        v(_future_shelley_delegs);
        v(_shelley_delegs);
        v(_shelley_delegs_schedule);
        v(_stake_pointers);

        v(_instant_rewards_reserves);
        v(_instant_rewards_treasury);

        v(_active_pool_params);
        v(_future_pool_params);
        v(_pools_retiring);
        v(_pool_deposits);
        v(_pool_vrf_key_hashes);

        v(_params);
        v(_params_prev);
        v(_nonmyopic);
        v(_nonmyopic_reward_pot);

        v(_delta_treasury);
        v(_delta_reserves);
        v(_reward_pot);
        v(_potential_rewards);
        v(_rewards_ready);
        v(_nonmyopic_next);

        v(_operating_stake_dist);
        v(_blocks_past_voting_deadline);
    }

    void state::to_zpp(zpp_encoder &sec) const
    {
        _visit([&](const auto &obj) {
           sec.add([&](auto) {
               return zpp::serialize(obj);
           });
        });
    }

    void state::from_zpp(parallel_decoder &dec)
    {
        _visit([&](auto &obj) {
            using T = std::decay_t<decltype(obj)>;
            dec.add([&](const auto b) {
                zpp::deserialize(const_cast<T &>(obj), b);
            });
        });
        dec.on_done([&] {
            _recompute_caches();
        });
    }

    void state::clear()
    {
        _visit([&](const auto &obj) {
            using T = std::decay_t<decltype(obj)>;
            auto &o = const_cast<T &>(obj);
            if constexpr (Clearable<decltype(o)>) {
                o.clear();
            } else if constexpr (std::is_same_v<decltype(o), bool>) {
                o = false;
            } else {
                o = 0;
            }
        });
    }

    const protocol_params &state::params() const
    {
        return _params;
    }
}
