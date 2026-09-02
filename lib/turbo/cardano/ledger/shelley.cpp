/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

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
        auto &v_it = v.array();
        if (!v_it.done()) {
            decode_versioned(v.at(0), [&](auto &dv) {
                auto &it = dv.array();
                _delta_treasury = it.read().uint();
                _delta_reserves = it.read().uint();
                _potential_rewards = map_from_cbor<decltype(_potential_rewards)>(it.read());
                if (!_potential_rewards.empty())
                    _rewards_ready = true;
                _delta_fees = it.read().uint();
                {
                    auto &nm_v = it.read();
                    auto &nm_it = nm_v.array();
                    _nonmyopic_next = decltype(_nonmyopic_next)::from_cbor(nm_it.read());
                    _reward_pot = nm_it.read().uint();
                }
            });
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

    void state::register_pool(const pool_reg_cert &reg)
    {
        const auto active_it = _active_pool_params.find(reg.pool_id);
        const auto pv11 = _params.protocol_ver.major >= 11;
        if (active_it == _active_pool_params.end()) {
            if (pv11 && _pool_vrf_key_hashes.contains(reg.params.vrf_vkey)) [[unlikely]]
                throw error(fmt::format("pool {} tried to register an already registered VRF key {}", reg.pool_id, reg.params.vrf_vkey));
            _active_pool_params.try_emplace(reg.pool_id, reg.params);
            if (pv11)
                _add_pool_vrf_key_hash(reg.params.vrf_vkey);
            _pool_deposits[reg.pool_id] = _params.pool_deposit;
            _deposited += _params.pool_deposit;
        } else {
            const auto future_it = _future_pool_params.find(reg.pool_id);
            const auto vrf_belongs_to_pool = reg.params.vrf_vkey == active_it->second.params.vrf_vkey
                || (future_it != _future_pool_params.end()
                    && reg.params.vrf_vkey == future_it->second.params.vrf_vkey);
            if (pv11 && !vrf_belongs_to_pool && _pool_vrf_key_hashes.contains(reg.params.vrf_vkey)) [[unlikely]] {
                throw error(fmt::format("pool {} tried to re-register an already registered VRF key {}", reg.pool_id, reg.params.vrf_vkey));
            }
            auto [f_it, f_created] = _future_pool_params.try_emplace(reg.pool_id, reg.params);
            if (pv11) {
                if (f_created) {
                    _add_pool_vrf_key_hash(reg.params.vrf_vkey);
                } else if (f_it->second.params.vrf_vkey != reg.params.vrf_vkey) {
                    _remove_pool_vrf_key_hash(f_it->second.params.vrf_vkey);
                    _add_pool_vrf_key_hash(reg.params.vrf_vkey);
                }
            }
            if (!f_created)
                f_it->second.params = reg.params;
        }
        // search for already delegated stake ids - needed for the case of re-registration of a retired pool
        if (_active_pool_dist.create(reg.pool_id)) {
            auto [inv_delegs_it, inv_delegs_created] = _active_inv_delegs.try_emplace(reg.pool_id);
            if (!inv_delegs_created) {
                for (const auto &stake_id: inv_delegs_it->second) {
                    const auto &acc = _accounts.at(stake_id);
                    _active_pool_dist.add(reg.pool_id, acc.stake + acc.reward);
                }
            }
        }
        // delete a planned retirement if present
        _pools_retiring.erase(reg.pool_id);
    }

    void state::retire_pool(const pool_hash &pool_id, uint64_t epoch)
    {
        if (_active_pool_params.contains(pool_id)) {
            _pools_retiring[pool_id] = epoch;
        } else {
            logger::warn("retirement of an unknown pool: {}", pool_id);
        }
    }

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

    void state::instant_reward_reserves(const stake_ident &stake_id, const uint64_t reward)
    {
        if (const auto prev_amount = _instant_rewards_reserves.get(stake_id); prev_amount > 0)
            _instant_rewards_reserves.sub(stake_id, prev_amount);
        _instant_rewards_reserves.add(stake_id, reward);
    }

    void state::instant_reward_treasury(const stake_ident &stake_id, const uint64_t reward)
    {

        if (const auto prev_amount = _instant_rewards_treasury.get(stake_id); prev_amount > 0)
            _instant_rewards_treasury.sub(stake_id, prev_amount);
        _instant_rewards_treasury.add(stake_id, reward);
    }

    static void _update_stake_delta(stake_update_map &deltas, const cardano::stake_ident &stake_id, const int64_t delta)
    {
        if (delta) {
            const auto [it, created] = deltas.try_emplace(stake_id, delta);
            if (!created) {
                it->second += delta;
                if (!it->second)
                    deltas.erase(it);
            }
        }
    }

    const tx_out_data *state::utxo_find(const tx_out_ref &txo_id)
    {
        if (const auto it = _utxo.find(txo_id);it != _utxo.end()) [[likely]]
            return &it->second;
        return nullptr;
    }

    void state::utxo_add(const cardano::tx_out_ref &txo_id, cardano::tx_out_data &&txo_data)
    {
        if (!txo_data.empty()) [[likely]] {
            auto [it, created] = _utxo.try_emplace(txo_id, std::move(txo_data));
            if (!created)
                logger::warn("a non-unique TXO {}!", it->first);
        }
    }

    void state::utxo_del(const cardano::tx_out_ref &txo_id)
    {
        if (const size_t num_del = _utxo.erase(txo_id); num_del != 1) [[unlikely]]
            throw error(fmt::format("request to remove an unknown TXO {}!", txo_id));
    }

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

    void state::process_cert(const cert_t &cert, const cert_loc_t &loc)
    {
        _tick(loc.slot);
        std::visit([&](const auto &c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, stake_reg_cert>
                    || std::is_same_v<T, stake_dereg_cert>
                    || std::is_same_v<T, stake_deleg_cert>
                    || std::is_same_v<T, pool_reg_cert>
                    || std::is_same_v<T, pool_retire_cert>
                    || std::is_same_v<T, genesis_deleg_cert>
                    || std::is_same_v<T, instant_reward_cert>) {
                process_cert(c, loc);
            } else {
                throw error(fmt::format("certificate type is not supported in shelley era: {}", typeid(T).name()));
            }
        }, cert.val);
    }

    void state::process_cert(const stake_reg_cert &c, const cert_loc_t &loc)
    {
        register_stake(loc.slot, c.stake_id, {}, loc.tx_idx, loc.cert_idx);
    }

    void state::process_cert(const stake_dereg_cert &c, const cert_loc_t &loc)
    {
        retire_stake(loc.slot, c.stake_id, {});
    }

    void state::process_cert(const stake_deleg_cert &c, const cert_loc_t &loc)
    {
        const auto acc_it = _accounts.find(c.stake_id);
        if (acc_it == _accounts.end() || !acc_it->second.ptr) [[unlikely]] {
            logger::debug("slot: {} stake_deleg_cert stake_id: {} pool_id: {} account_known: {} registered: {} reward: {} deposit: {} delegated: {} tx_idx: {} cert_idx: {}",
                cardano::slot { loc.slot, _cfg }, c.stake_id, c.pool_id,
                acc_it != _accounts.end(),
                acc_it != _accounts.end() && acc_it->second.ptr.has_value(),
                cardano::amount { acc_it != _accounts.end() ? acc_it->second.reward : 0 },
                cardano::amount { acc_it != _accounts.end() ? acc_it->second.deposit : 0 },
                acc_it != _accounts.end() && acc_it->second.deleg.has_value(),
                loc.tx_idx, loc.cert_idx);
        }
        delegate_stake(c.stake_id, c.pool_id);
    }

    void state::process_cert(const pool_reg_cert &c, const cert_loc_t &)
    {
        register_pool(c);
    }

    void state::process_cert(const pool_retire_cert &c, const cert_loc_t &)
    {
        retire_pool(c.pool_id, c.epoch);
    }

    void state::process_cert(const genesis_deleg_cert &c, const cert_loc_t &loc)
    {
        genesis_deleg_update(loc.slot, c.hash, c.pool_id, c.vrf_vkey);
    }

    void state::process_cert(const instant_reward_cert &c, const cert_loc_t &)
    {
        for (const auto &[stake_id, coin]: c.rewards) {
            if (c.source == reward_source::reserves)
                instant_reward_reserves(stake_id, coin);
            else if (c.source == reward_source::treasury)
                instant_reward_treasury(stake_id, coin);
            else
                throw error(fmt::format("unsupported reward source: {}", static_cast<int>(c.source)));
        }
    }

    void state::_process_timed_update(tx_out_ref_list &collected_collateral, uint64_t &collateral_refund, timed_update_t &&upd)
    {
        std::visit([&](const auto &u) {
            using T = std::decay_t<decltype(u)>;
            if constexpr (std::is_same_v<T, index::timed_update::stake_withdraw>) {
                withdraw_reward(u.stake_id, u.amount);
            } else if constexpr (std::is_same_v<T, param_update_proposal>) {
                propose_update(upd.loc.slot, u);
            } else if constexpr (std::is_same_v<T, param_update_vote>) {
                proposal_vote(upd.loc.slot, u);
            } else if constexpr (std::is_same_v<T, index::timed_update::collected_collateral_input>) {
                collected_collateral.emplace_back(u.tx_hash, u.txo_idx);
            } else if constexpr (std::is_same_v<T, index::timed_update::collected_collateral_refund>) {
                collateral_refund += u.refund;
            } else if constexpr (std::is_same_v<T, stake_reg_cert>
                || std::is_same_v<T, stake_dereg_cert>
                || std::is_same_v<T, stake_deleg_cert>
                || std::is_same_v<T, pool_reg_cert>
                || std::is_same_v<T, pool_retire_cert>
                || std::is_same_v<T, genesis_deleg_cert>
                || std::is_same_v<T, instant_reward_cert>) {
                process_cert(cert_t { u }, upd.loc);
            } else {
                throw error(fmt::format("unsupported timed update: {}", typeid(u).name()));
            }
        }, upd.update);
    }

    std::pair<tx_out_ref_list, uint64_t> state::_process_timed_updates(timed_update_list &&timed_updates)
    {
        timer tp { fmt::format("validator epoch: {} process {} timed updates", _epoch, timed_updates.size()) };
        std::vector<tx_out_ref> collected_collateral {};
        uint64_t collateral_refund = 0;
        for (auto &&upd: timed_updates) {
            _process_timed_update(collected_collateral, collateral_refund, std::move(upd));
        }
        return { std::move(collected_collateral), collateral_refund };
    }

    void state::_process_utxo_updates(utxo_update_list &&utxo_updates)
    {
        using account_map = partitioned_map<stake_ident, account_info>;
        constexpr size_t num_account_parts = account_map::num_parts;
        using diagnostic_clock = std::chrono::steady_clock;
        struct utxo_partition_diagnostics {
            uint64_t elapsed_ns = 0;
            size_t updates = 0;
            size_t stake_deltas = 0;
            size_t pointer_deltas = 0;
        };
        struct stake_partition_diagnostics {
            uint64_t elapsed_ns = 0;
            size_t partial_deltas = 0;
            size_t unique_deltas = 0;
        };
        struct destroy_diagnostics {
            uint64_t elapsed_ns = 0;
            size_t entries = 0;
        };
        struct partitioned_stake_deltas {
            std::vector<std::pair<stake_ident, int64_t>> values {};
            std::array<size_t, account_map::num_parts + 1> offsets {};
        };
        using pool_delta_map = std::map<pool_hash, int64_t>;
        const auto elapsed_ns = [](const diagnostic_clock::time_point start) {
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(diagnostic_clock::now() - start).count());
        };

        const std::string utxo_task_group = fmt::format("ledger-state:apply-utxo-updates:epoch-{}", _epoch);
        const std::string stake_task_group = fmt::format("ledger-state:apply-stake-deltas:epoch-{}", _epoch);
        std::array<partitioned_stake_deltas, txo_map::num_parts> stake_deltas_by_source {};
        std::array<pointer_update_map, txo_map::num_parts> pointer_deltas_by_source {};
        std::array<pool_delta_map, num_account_parts> pool_deltas_by_account_part {};
        std::array<utxo_partition_diagnostics, txo_map::num_parts> utxo_part_diag {};
        std::array<stake_partition_diagnostics, num_account_parts> stake_part_diag {};
        const auto total_start = diagnostic_clock::now();
        const auto utxo_phase_start = diagnostic_clock::now();
        {
            turbo::timer t { fmt::format("validator epoch {} apply utxo partitions batches: {}", _epoch, utxo_updates.size()), logger::level::trace };
            _sched.wait_all(utxo_task_group,
                [&](const auto &todo, const auto &submit_f) {
                    for (size_t part_idx = 0; part_idx < txo_map::num_parts; ++part_idx) {
                        submit_f({ 1000, utxo_task_group, [this, part_idx, todo, &utxo_updates, &stake_deltas_by_source,
                                &pointer_deltas_by_source, &utxo_part_diag, &elapsed_ns] {
                            const auto task_start = diagnostic_clock::now();
                            size_t num_updates = 0;
                            stake_update_map deltas {};
                            pointer_update_map pointer_deltas {};
                            for (auto &&update_batch: utxo_updates) {
                                auto &upd_part = update_batch.partition(part_idx);
                                num_updates += upd_part.size();
                                auto &utxo_part = _utxo.partition(part_idx);
                                for (auto &&[txo_id, txo_data]: upd_part) {
                                    if (!txo_data.address_raw.empty()) {
                                        const auto addr = txo_data.addr();
                                        if (addr.has_stake_id()) [[likely]]
                                            _update_stake_delta(deltas, addr.stake_id(), static_cast<int64_t>(txo_data.coin));
                                        else if (addr.has_pointer()) [[unlikely]]
                                            pointer_deltas[addr.pointer()] += static_cast<int64_t>(txo_data.coin);
                                        if (!txo_data.empty()) [[likely]] {
                                            if (auto [it, created] = utxo_part.try_emplace(txo_id, std::move(txo_data)); !created) [[unlikely]]
                                                logger::warn("a non-unique TXO {}!", it->first);
                                        }
                                    } else {
                                        if (auto it = utxo_part.find(txo_id); it != utxo_part.end()) [[likely]] {
                                            const auto addr = it->second.addr();
                                            if (addr.has_stake_id()) [[likely]]
                                                _update_stake_delta(deltas, addr.stake_id(), -static_cast<int64_t>(it->second.coin));
                                            else if (addr.has_pointer()) [[unlikely]]
                                                pointer_deltas[addr.pointer()] -= static_cast<int64_t>(it->second.coin);
                                            utxo_part.erase(it);
                                        } else {
                                            throw error(fmt::format("request to remove an unknown TXO {}!", txo_id));
                                        }
                                    }
                                }
                            }
                            auto &partitioned_deltas = stake_deltas_by_source[part_idx];
                            for (const auto &[stake_id, delta]: deltas)
                                ++partitioned_deltas.offsets[account_map::partition_idx(stake_id) + 1];
                            for (size_t dest_part_idx = 1; dest_part_idx < partitioned_deltas.offsets.size(); ++dest_part_idx)
                                partitioned_deltas.offsets[dest_part_idx] += partitioned_deltas.offsets[dest_part_idx - 1];
                            auto positions = partitioned_deltas.offsets;
                            partitioned_deltas.values.resize(deltas.size());
                            for (const auto &[stake_id, delta]: deltas) {
                                const auto dest_part_idx = account_map::partition_idx(stake_id);
                                partitioned_deltas.values[positions[dest_part_idx]++] = { stake_id, delta };
                            }
                            const auto num_pointer_deltas = pointer_deltas.size();
                            pointer_deltas_by_source[part_idx] = std::move(pointer_deltas);
                            utxo_part_diag[part_idx] = {
                                .elapsed_ns=elapsed_ns(task_start),
                                .updates=num_updates,
                                .stake_deltas=partitioned_deltas.values.size(),
                                .pointer_deltas=num_pointer_deltas
                            };
                        }});
                    }
                });
        }
        const auto utxo_phase_ns = elapsed_ns(utxo_phase_start);
        const auto num_utxo_update_batches = utxo_updates.size();
        std::vector<destroy_diagnostics> destroy_diag(num_utxo_update_batches);
        const auto stake_phase_start = diagnostic_clock::now();
        {
            turbo::timer t { fmt::format("validator epoch {} apply stake delta partitions and destroy utxo update maps", _epoch),
                logger::level::trace };
            _sched.wait_all(stake_task_group,
                [&](const auto &todo, const auto &submit_f) {
                    for (size_t part_idx = 0; part_idx < num_account_parts; ++part_idx) {
                        submit_f({ 1000, stake_task_group, [this, part_idx, todo, &stake_deltas_by_source,
                                &pool_deltas_by_account_part, &stake_part_diag, &elapsed_ns] {
                            const auto task_start = diagnostic_clock::now();
                            size_t num_partial_deltas = 0;
                            for (const auto &source_deltas: stake_deltas_by_source) {
                                num_partial_deltas += source_deltas.offsets[part_idx + 1]
                                    - source_deltas.offsets[part_idx];
                            }
                            stake_update_map deltas {};
                            deltas.reserve(num_partial_deltas);
                            for (const auto &source_deltas: stake_deltas_by_source) {
                                for (size_t delta_idx = source_deltas.offsets[part_idx];
                                        delta_idx < source_deltas.offsets[part_idx + 1]; ++delta_idx) {
                                    const auto &[stake_id, delta] = source_deltas.values[delta_idx];
                                    _update_stake_delta(deltas, stake_id, delta);
                                }
                            }
                            auto &account_part = _accounts.partition(part_idx);
                            auto &pool_deltas = pool_deltas_by_account_part[part_idx];
                            const auto num_unique_deltas = deltas.size();
                            for (const auto &[stake_id, delta]: deltas) {
                                auto &acc = account_part[stake_id];
                                if (delta >= 0) {
                                    acc.stake += static_cast<uint64_t>(delta);
                                } else {
                                    const uint64_t dec = static_cast<uint64_t>(-delta);
                                    if (acc.stake < dec) [[unlikely]] {
                                        throw error(fmt::format("trying to remove from account {} more stake {} than it has: {}",
                                            stake_id, dec, acc.stake));
                                    }
                                    acc.stake -= dec;
                                }
                                if (acc.deleg && _active_pool_params.contains(*acc.deleg)) {
                                    const auto [it, created] = pool_deltas.try_emplace(*acc.deleg, delta);
                                    if (!created) {
                                        it->second += delta;
                                        if (!it->second)
                                            pool_deltas.erase(it);
                                    }
                                }
                            }
                            stake_part_diag[part_idx] = {
                                .elapsed_ns=elapsed_ns(task_start),
                                .partial_deltas=num_partial_deltas,
                                .unique_deltas=num_unique_deltas
                            };
                        }});
                    }
                    // The preceding UTXO-partition barrier is the last reader of these maps.
                    // Retire them alongside the stake-delta tasks so both operations share one wait cycle.
                    for (size_t batch_idx = 0; batch_idx < num_utxo_update_batches; ++batch_idx) {
                        submit_f({ 1000, stake_task_group, [batch_idx, todo, &utxo_updates, &destroy_diag, &elapsed_ns] {
                            const auto task_start = diagnostic_clock::now();
                            const auto num_entries = utxo_updates[batch_idx].size();
                            utxo_updates[batch_idx].clear();
                            destroy_diag[batch_idx] = {
                                .elapsed_ns=elapsed_ns(task_start),
                                .entries=num_entries
                            };
                        }});
                    }
                });
        }
        const auto stake_phase_ns = elapsed_ns(stake_phase_start);
        const auto cleanup_start = diagnostic_clock::now();
        utxo_updates.clear();
        const auto cleanup_ns = elapsed_ns(cleanup_start);
        size_t pool_partial_entries = 0;
        size_t pool_merged_entries = 0;
        size_t pointer_partial_entries = 0;
        size_t pointer_merged_entries = 0;
        uint64_t pool_merge_ns = 0;
        uint64_t pointer_merge_ns = 0;
        {
            const auto pool_merge_start = diagnostic_clock::now();
            pool_delta_map all_pool_deltas {};
            for (const auto &pool_deltas: pool_deltas_by_account_part) {
                pool_partial_entries += pool_deltas.size();
                for (const auto &[pool_id, delta]: pool_deltas) {
                    const auto [it, created] = all_pool_deltas.try_emplace(pool_id, delta);
                    if (!created) {
                        it->second += delta;
                        if (!it->second)
                            all_pool_deltas.erase(it);
                    }
                }
            }
            for (const auto &[pool_id, delta]: all_pool_deltas) {
                if (delta >= 0)
                    _active_pool_dist.add(pool_id, static_cast<uint64_t>(delta));
                else
                    _active_pool_dist.sub(pool_id, static_cast<uint64_t>(-delta));
            }
            pool_merged_entries = all_pool_deltas.size();
            pool_merge_ns = elapsed_ns(pool_merge_start);

            const auto pointer_merge_start = diagnostic_clock::now();
            pointer_update_map all_pointer_deltas {};
            for (const auto &pointer_deltas: pointer_deltas_by_source) {
                pointer_partial_entries += pointer_deltas.size();
                for (const auto &[stake_ptr, delta]: pointer_deltas) {
                    const auto [it, created] = all_pointer_deltas.try_emplace(stake_ptr, delta);
                    if (!created) {
                        it->second += delta;
                        if (!it->second)
                            all_pointer_deltas.erase(it);
                    }
                }
            }
            for (const auto &[stake_ptr, delta]: all_pointer_deltas)
                update_pointer(stake_ptr, delta);
            pointer_merged_entries = all_pointer_deltas.size();
            pointer_merge_ns = elapsed_ns(pointer_merge_start);
        }

        uint64_t utxo_task_sum_ns = 0;
        uint64_t utxo_task_max_ns = 0;
        size_t utxo_task_max_part = 0;
        size_t total_updates = 0;
        size_t total_stake_deltas = 0;
        size_t total_pointer_deltas = 0;
        for (size_t part_idx = 0; part_idx < utxo_part_diag.size(); ++part_idx) {
            const auto &diag = utxo_part_diag[part_idx];
            utxo_task_sum_ns += diag.elapsed_ns;
            total_updates += diag.updates;
            total_stake_deltas += diag.stake_deltas;
            total_pointer_deltas += diag.pointer_deltas;
            if (diag.elapsed_ns > utxo_task_max_ns) {
                utxo_task_max_ns = diag.elapsed_ns;
                utxo_task_max_part = part_idx;
            }
        }
        uint64_t stake_task_sum_ns = 0;
        uint64_t stake_task_max_ns = 0;
        size_t stake_task_max_part = 0;
        size_t total_partial_deltas = 0;
        size_t total_unique_deltas = 0;
        for (size_t part_idx = 0; part_idx < stake_part_diag.size(); ++part_idx) {
            const auto &diag = stake_part_diag[part_idx];
            stake_task_sum_ns += diag.elapsed_ns;
            total_partial_deltas += diag.partial_deltas;
            total_unique_deltas += diag.unique_deltas;
            if (diag.elapsed_ns > stake_task_max_ns) {
                stake_task_max_ns = diag.elapsed_ns;
                stake_task_max_part = part_idx;
            }
        }
        uint64_t destroy_task_sum_ns = 0;
        uint64_t destroy_task_max_ns = 0;
        size_t destroy_task_max_batch = 0;
        for (size_t batch_idx = 0; batch_idx < destroy_diag.size(); ++batch_idx) {
            const auto &diag = destroy_diag[batch_idx];
            destroy_task_sum_ns += diag.elapsed_ns;
            if (diag.elapsed_ns > destroy_task_max_ns) {
                destroy_task_max_ns = diag.elapsed_ns;
                destroy_task_max_batch = batch_idx;
            }
        }
        logger::debug(
            "ledger UTXO diagnostics epoch: {} batches: {} updates: {} "
            "utxo_wall_ms: {:.3f} utxo_task_sum_ms: {:.3f} utxo_task_max_ms: {:.3f} utxo_task_max_part: {} "
            "utxo_task_max_updates: {} utxo_task_max_stake_deltas: {} utxo_task_max_pointer_deltas: {} "
            "stake_deltas: {} pointer_deltas: {} "
            "stake_wall_ms: {:.3f} stake_task_sum_ms: {:.3f} stake_task_max_ms: {:.3f} stake_task_max_part: {} "
            "stake_task_max_partial_deltas: {} stake_task_max_unique_deltas: {} partial_deltas: {} unique_deltas: {} "
            "destroy_task_sum_ms: {:.3f} destroy_task_max_ms: {:.3f} destroy_task_max_batch: {} destroy_task_max_entries: {} "
            "cleanup_ms: {:.3f} pool_merge_ms: {:.3f} pool_partial_entries: {} pool_merged_entries: {} "
            "pointer_merge_ms: {:.3f} pointer_partial_entries: {} pointer_merged_entries: {} total_ms: {:.3f}",
            _epoch, num_utxo_update_batches, total_updates,
            static_cast<double>(utxo_phase_ns) / 1'000'000,
            static_cast<double>(utxo_task_sum_ns) / 1'000'000,
            static_cast<double>(utxo_task_max_ns) / 1'000'000,
            utxo_task_max_part, utxo_part_diag[utxo_task_max_part].updates,
            utxo_part_diag[utxo_task_max_part].stake_deltas,
            utxo_part_diag[utxo_task_max_part].pointer_deltas,
            total_stake_deltas, total_pointer_deltas,
            static_cast<double>(stake_phase_ns) / 1'000'000,
            static_cast<double>(stake_task_sum_ns) / 1'000'000,
            static_cast<double>(stake_task_max_ns) / 1'000'000,
            stake_task_max_part, stake_part_diag[stake_task_max_part].partial_deltas,
            stake_part_diag[stake_task_max_part].unique_deltas,
            total_partial_deltas, total_unique_deltas,
            static_cast<double>(destroy_task_sum_ns) / 1'000'000,
            static_cast<double>(destroy_task_max_ns) / 1'000'000,
            destroy_task_max_batch,
            destroy_diag.empty() ? 0 : destroy_diag[destroy_task_max_batch].entries,
            static_cast<double>(cleanup_ns) / 1'000'000,
            static_cast<double>(pool_merge_ns) / 1'000'000,
            pool_partial_entries, pool_merged_entries,
            static_cast<double>(pointer_merge_ns) / 1'000'000,
            pointer_partial_entries, pointer_merged_entries,
            static_cast<double>(elapsed_ns(total_start)) / 1'000'000);
    }

    void state::_process_collateral_use(tx_out_ref_list &&collected_collateral)
    {
        for (const auto &txo_id: collected_collateral) {
            const auto txo_data = utxo_find(txo_id);
            if (!txo_data) [[unlikely]]
                throw error(fmt::format("epoch {}: cannot find data about a TXO used as a collateral input: {}", _epoch, txo_id));
            add_fees(txo_data->coin);
            if (const auto addr = txo_data->addr(); addr.has_stake_id_hybrid()) [[likely]]
                update_stake_id_hybrid(addr.stake_id_hybrid(), -static_cast<int64_t>(txo_data->coin));
            utxo_del(txo_id);
        }
    }

    void state::process_updates(updates_t &&updates)
    {
        const auto last_slot = updates.blocks.empty()
            ? std::optional<uint64_t> {} : std::optional<uint64_t> { updates.blocks.back().slot };
        {
            turbo::timer t { fmt::format("validator epoch {} process block updates: {}", _epoch, updates.blocks.size()), logger::level::trace };
            _process_block_updates(std::move(updates.blocks));
        }
        tx_out_ref_list collected_collateral {};
        uint64_t collateral_refund = 0;
        {
            turbo::timer t { fmt::format("validator epoch {} process timed updates: {}", _epoch, updates.timed.size()), logger::level::trace };
            auto collateral_updates = _process_timed_updates(std::move(updates.timed));
            collected_collateral = std::move(collateral_updates.first);
            collateral_refund = collateral_updates.second;
        }
        if (last_slot)
            _tick(*last_slot);
        {
            turbo::timer t { fmt::format("validator epoch {} process utxo updates batches: {}", _epoch, updates.utxos.size()), logger::level::trace };
            _process_utxo_updates(std::move(updates.utxos));
        }
        {
            turbo::timer t { fmt::format("validator epoch {} process collateral uses: {}", _epoch, collected_collateral.size()), logger::level::trace };
            _process_collateral_use(std::move(collected_collateral));
        }
        if (collateral_refund) {
            turbo::timer t { fmt::format("validator epoch {} process collateral refund: {}", _epoch, collateral_refund), logger::level::trace };
            sub_fees(collateral_refund);
        }
        run_pulser_if_ready();
    }

    void state::run_pulser_if_ready()
    {
        if (_params.protocol_ver.major >= 2 && !_rewards_ready) {
            const auto slot = cardano::slot::from_epoch(_epoch, _cfg) + _epoch_slot;
            _ensure_reward_pulsing_snapshot(slot);
        }
        const auto run = _params.protocol_ver.major >= 2 && _epoch_slot >= _cfg.shelley_rewards_ready_slot && !_rewards_ready;
        if (run)
            _compute_rewards();
    }

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

    void state::withdraw_reward(const stake_ident &stake_id, const uint64_t amount)
    {
        auto &acc = _accounts.at(stake_id);
        logger::trace("withdraw_reward stake_id: {} amount: {} reward: {} deposit: {} registered: {} delegated: {}",
            stake_id, cardano::amount { amount }, cardano::amount { acc.reward }, cardano::amount { acc.deposit },
            acc.ptr.has_value(), acc.deleg.has_value());
        if (acc.reward != amount) [[unlikely]] {
            logger::debug("withdraw_reward mismatch epoch: {} stake_id: {} amount: {} reward: {} difference: {} registered: {} ptr: {} deposit: {} stake: {} mark_stake: {} set_stake: {} go_stake: {} deleg: {} mark_deleg: {} set_deleg: {} go_deleg: {} active_pool_dist: {}",
                _epoch, stake_id, cardano::amount { amount }, cardano::amount { acc.reward },
                cardano::amount { amount > acc.reward ? amount - acc.reward : acc.reward - amount }, acc.ptr.has_value(), acc.ptr,
                cardano::amount { acc.deposit }, cardano::amount { acc.stake },
                cardano::amount { acc.mark_stake }, cardano::amount { acc.set_stake },
                cardano::amount { acc.go_stake }, acc.deleg, acc.mark_deleg,
                acc.set_deleg, acc.go_deleg,
                cardano::amount { acc.deleg ? _active_pool_dist.get(*acc.deleg) : 0 });
            throw error(fmt::format("withdrawal from account {} does not drain its reward balance: requested {} but available {}", stake_id, amount, acc.reward));
        }
        acc.reward -= amount;
        if (acc.deleg)
            _active_pool_dist.sub(*acc.deleg, amount);
    }

    void state::_withdraw_reward(const reward_id_t &reward_id, const uint64_t amount)
    {
        if (reward_id.network_id() != _cfg.shelley_network_id) [[unlikely]] {
            throw error(fmt::format(
                "withdrawal reward address has network id {} but expected {}",
                reward_id.network_id(),
                _cfg.shelley_network_id));
        }
        withdraw_reward(static_cast<stake_ident>(reward_id), amount);
    }

    void state::register_stake(const uint64_t slot, const stake_ident &stake_id, const std::optional<uint64_t> deposit, const size_t tx_idx, const size_t cert_idx)
    {
        //logger::debug("slot: {} shelley::register_stake {} deposit: {}", cardano::slot { slot, _cfg }, stake_id, deposit);
        const auto deposit_size = deposit ? *deposit : _params.key_deposit;
        auto [acc_it, acc_created] = _accounts.try_emplace(stake_id);
        if (acc_created || !acc_it->second.ptr) {
            _deposited += deposit_size;
            acc_it->second.deposit += deposit_size;
        }
        stake_pointer ptr { slot, tx_idx, cert_idx };
        _ptr_to_stake[ptr] = stake_id;
        acc_it->second.ptr = ptr;
    }

    void state::retire_stake(const uint64_t slot, const stake_ident &stake_id, const std::optional<uint64_t> deposit)
    {
        //logger::debug("slot: {} shelley::retire_stake id: {} deposit: {}", slot, stake_id, deposit);
        const auto deposit_size = deposit ? *deposit : _params.key_deposit;
        auto &acc = _accounts.at(stake_id);
        logger::trace("retire_stake stake_id: {} slot: {} cert_deposit: {} account_reward: {} account_deposit: {} registered: {} delegated: {}",
            stake_id, cardano::slot { slot, _cfg }, cardano::amount { deposit_size }, cardano::amount { acc.reward },
            cardano::amount { acc.deposit }, acc.ptr.has_value(), acc.deleg.has_value());
        if (acc.ptr) {
            if (acc.deposit >= deposit_size) [[likely]]
                acc.deposit -= deposit_size;
            else
                throw error(fmt::format("expected stake deposit: {} is more than the actual one: {}", deposit_size, acc.deposit));
            if (_deposited >= deposit_size) [[likely]]
                _deposited -= deposit_size;
            else
                throw error("trying to remove a deposit while having insufficient deposits");
            _ptr_to_stake.erase(*acc.ptr);
            acc.ptr.reset();
        } else {
            logger::trace("slot: {}/{} can't find the retiring stake's pointer", _epoch, slot);
        }
        if (acc.deleg) {
            const auto stake = acc.stake + acc.reward;
            //logger::trace("epoch: {} retirement of {} - removing {} from pool {}", _epoch, stake_id, cardano::amount { stake }, deleg_it->second);
            _active_pool_dist.sub(*acc.deleg, stake);
            _active_inv_delegs.at(*acc.deleg).erase(stake_id);
        }
        _treasury += acc.reward;
        acc.reward = 0;
        acc.deleg.reset();
    }

    void state::delegate_stake(const stake_ident &stake_id, const pool_hash &pool_id)
    {
        const auto pool_known = _active_pool_params.contains(pool_id);
        const auto acc_it = _accounts.find(stake_id);
        if (!pool_known || acc_it == _accounts.end() || !acc_it->second.ptr) [[unlikely]] {
            logger::debug("delegate_stake stake_id: {} pool_id: {} pool_known: {} account_known: {} registered: {} reward: {} deposit: {} delegated: {}",
                stake_id, pool_id, pool_known,
                acc_it != _accounts.end(),
                acc_it != _accounts.end() && acc_it->second.ptr.has_value(),
                cardano::amount { acc_it != _accounts.end() ? acc_it->second.reward : 0 },
                cardano::amount { acc_it != _accounts.end() ? acc_it->second.deposit : 0 },
                acc_it != _accounts.end() && acc_it->second.deleg.has_value());
        }
        if (!pool_known) [[unlikely]]
            throw error(fmt::format("trying to delegate {} to an unknown pool: {}", stake_id, pool_id));
        auto &acc = _accounts.at(stake_id);
        const auto stake = acc.stake + acc.reward;
        const bool deleg_created = !acc.deleg;
        if (!acc.deleg)
            acc.deleg = pool_id;
        if (deleg_created || *acc.deleg != pool_id) {
            _active_inv_delegs[pool_id].emplace(stake_id);
            _active_pool_dist.add(pool_id, stake);
        }
        if (*acc.deleg != pool_id) {
            _active_inv_delegs[*acc.deleg].erase(stake_id);
            // ignore retired pools
            if (_active_pool_params.contains(*acc.deleg)) {
                _active_pool_dist.sub(*acc.deleg, stake);
            }
            *acc.deleg = pool_id;
        }
    }

    void state::update_stake(const stake_ident &stake_id, const int64_t delta)
    {
        auto &acc = _accounts[stake_id];
        if (delta >= 0) {
            acc.stake += static_cast<uint64_t>(delta);
            if (acc.deleg && _active_pool_params.contains(*acc.deleg))
                _active_pool_dist.add(*acc.deleg, static_cast<uint64_t>(delta));
        } else {
            const uint64_t dec = static_cast<uint64_t>(-delta);
            if (acc.stake < dec) [[unlikely]]
                throw error(fmt::format("trying to remove from account {} more stake {} than it has: {}", stake_id, dec, acc.stake));
            acc.stake -= dec;
            if (acc.deleg && _active_pool_params.contains(*acc.deleg))
                _active_pool_dist.sub(*acc.deleg, static_cast<uint64_t>(-delta));
        }
    }

    void state::update_pointer(const cardano::stake_pointer &ptr, const int64_t delta)
    {
        /*if (const auto ptr_it = _ptr_to_stake.find(ptr); ptr_it != _ptr_to_stake.end()) {
            logger::trace("epoch: {} stake update via pointer: {} {} delta: {}", _epoch, ptr, ptr_it->second, cardano::balance_change { delta });
            update_stake(ptr_it->second, delta);
        } else { */
        if (delta) {
            if (delta >= 0)
                _stake_pointers.add(ptr, delta);
            else if (_stake_pointers.contains(ptr))
                _stake_pointers.sub(ptr, static_cast<uint64_t>(-delta));
            else
                logger::warn("epoch: {} skipping an unknown stake pointer: {} delta: {}", _epoch, ptr, cardano::balance_change { delta });
        }
    }

    void state::update_stake_id_hybrid(const cardano::stake_ident_hybrid &stake_id, const int64_t delta)
    {
        if (delta) {
            if (std::holds_alternative<cardano::stake_ident>(stake_id))
                update_stake(std::get<cardano::stake_ident>(stake_id), delta);
            else if (std::holds_alternative<cardano::stake_pointer>(stake_id))
                update_pointer(std::get<cardano::stake_pointer>(stake_id), delta);
            else
                throw error("internal error: an unexpected value for a stake_indent!");
        }
    }

    void state::proposal_vote(const uint64_t, const cardano::param_update_vote &vote)
    {
        // needed only for Byron-era voting, and all updates are in the current epoch
        for (const auto &[pool_id, prop]: _ppups) {
            if (prop.hash == vote.proposal_id) {
                _ppups[vote.key_id] = prop;
                break;
            }
        }
    }

    void state::propose_update(const uint64_t slot, const cardano::param_update_proposal &prop)
    {
        if (_params.protocol_ver.major >= 2) {
            if (!_cfg.shelley_delegates.contains(prop.key_id)) [[unlikely]]
                throw error(fmt::format("protocol update proposal from a key not in the shelley genesis delegate list: {}!", prop.key_id));
            if (!prop.epoch || *prop.epoch == _epoch) {
                const auto too_late = cardano::slot::from_epoch(_epoch + 1, _cfg) - 2 * _cfg.shelley_stability_window;
                if (slot < too_late) {
                    _ppups[prop.key_id] = prop.update;
                } else {
                    logger::warn("epoch: {} slot: {} ignoring an update proposal since its too late in the epoch", _epoch, slot);
                }
            } else if (*prop.epoch == _epoch + 1) {
                _ppups_future[prop.key_id] = prop.update;
            } else {
                logger::warn("epoch: {} slot: {} ignoring an update proposal for an unexpected epoch: {}", _epoch, slot, *prop.epoch);
            }
        } else {
            _ppups[prop.key_id] = prop.update;
        }
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

    void state::sub_fees(const uint64_t refund)
    {
        if (_fees_next_reward >= refund) [[likely]]
            _fees_next_reward -= refund;
        else
            throw error(fmt::format("insufficient fees_next_reward: {} to refund {}", _fees_next_reward, refund));
        if (_fees_utxo >= refund) [[likely]]
            _fees_utxo -= refund;
        else
            throw error(fmt::format("insufficient fees_utxo: {} to refund {}", _fees_utxo, refund));
    }

    void state::add_fees(const uint64_t amount)
    {
        _fees_next_reward += amount;
        _fees_utxo += amount;
    }

    const shelley_delegate_map &state::shelley_delegs() const
    {
        return _shelley_delegs;
    }

    const shelley_delegate_schedule &state::shelley_delegs_schedule() const
    {
        return _shelley_delegs_schedule;
    }

    void state::genesis_deleg_update(const uint64_t slot, const cardano::key_hash &hash,
        const cardano::pool_hash &pool_id, const cardano::vrf_vkey &vrf_vkey)
    {
        if (!_shelley_delegs.contains(hash)) [[unlikely]]
            throw error(fmt::format("an attempt to redelegate an unknown shelley genesis delegate {}", hash));
        for (const auto &[genesis, deleg]: _shelley_delegs) {
            if (genesis != hash && deleg.delegate == pool_id) [[unlikely]]
                throw error(fmt::format("shelley genesis delegate {} is already active", pool_id));
            if (genesis != hash && deleg.vrf == vrf_vkey) [[unlikely]]
                throw error(fmt::format("shelley genesis VRF key {} is already active", vrf_vkey));
        }
        for (const auto &[future, deleg]: _future_shelley_delegs) {
            if (future.genesis != hash && deleg.delegate == pool_id) [[unlikely]]
                throw error(fmt::format("shelley genesis delegate {} is already scheduled", pool_id));
            if (future.genesis != hash && deleg.vrf == vrf_vkey) [[unlikely]]
                throw error(fmt::format("shelley genesis VRF key {} is already scheduled", vrf_vkey));
        }
        if (slot > std::numeric_limits<uint64_t>::max() - _cfg.shelley_stability_window) [[unlikely]]
            throw error("shelley genesis delegation activation slot overflows");
        _future_shelley_delegs.insert_or_assign(
            future_shelley_delegate { slot + _cfg.shelley_stability_window, hash },
            shelley_delegate { pool_id, vrf_vkey });
    }

    void state::_apply_future_shelley_delegs(const uint64_t slot)
    {
        auto it = _future_shelley_delegs.begin();
        bool changed = false;
        while (it != _future_shelley_delegs.end() && it->first.slot <= slot) {
            changed = true;
            const auto activation = it->first.slot;
            do {
                _shelley_delegs.at(it->first.genesis) = it->second;
                it = _future_shelley_delegs.erase(it);
            } while (it != _future_shelley_delegs.end() && it->first.slot == activation);
            _shelley_delegs_schedule.changes.insert_or_assign(activation, _shelley_delegs);
        }
        if (changed)
            _pbft_pools = _make_pbft_pools(_shelley_delegs);
    }

    void state::_reset_shelley_delegs_schedule(const bool complete)
    {
        _shelley_delegs_schedule = {
            .complete=complete,
            .initial=_shelley_delegs,
            .changes={}
        };
    }

    void state::rotate_snapshots()
    {
        timer t { fmt::format("validator::state epoch: {} rotate_snapshots", _epoch), logger::level::trace };
        auto retired_go = std::make_shared<std::optional<ledger_copy>>(std::in_place, std::move(_go));
        _go = std::move(_set);
        _set = std::move(_mark);
        {
            const std::string task_group = fmt::format("ledger-state:rotate-snapshots:epoch-{}", _epoch);
            _sched.wait_all(task_group, [&](const auto &, const auto &submit_f) {
                submit_f({ 1001, task_group, [retired_go] {
                    retired_go->reset();
                }});
                retired_go.reset();
                submit_f({ 1000, task_group, [this] {
                    _mark.pool_dist = _active_pool_dist;
                }});
                submit_f({ 1000, task_group, [this] {
                    _mark.pool_params = _active_pool_params;
                }});
                submit_f({ 1000, task_group, [this] {
                    _mark.delegated_pools.clear();
                    _mark.delegated_pools.reserve(_active_inv_delegs.size());
                    // pool_hash ordering is lexicographic, while partitioned_map uses its first byte.
                    // Concatenating partitions 0..255 therefore produces the sorted sequence expected
                    // by static_map without copying the historical delegator sets.
                    for (size_t pi = 0; pi < inv_delegation_map::num_parts; ++pi) {
                        for (const auto &[pool_id, delegators]: _active_inv_delegs.partition(pi)) {
                            if (!delegators.empty())
                                _mark.delegated_pools.emplace_back(pool_id, true);
                        }
                    }
                }});
                for (size_t pi = 0; pi < _accounts.num_parts; ++pi) {
                    submit_f({ 1000, task_group, [this, pi] {
                        auto &part = _accounts.partition(pi);
                        std::set<stake_ident> retired {};
                        for (auto &[stake_id, acc]: part) {
                            if (acc.ptr || acc.stake || acc.reward || acc.deposit || acc.go_deleg
                                    || acc.set_deleg || acc.mark_deleg || acc.deleg || acc.vote_deleg) {
                                acc.go_deleg = acc.set_deleg;
                                acc.go_stake = acc.set_stake;
                                acc.set_deleg = acc.mark_deleg;
                                acc.set_stake = acc.mark_stake;
                                acc.mark_deleg = acc.deleg;
                                acc.mark_stake = acc.stake + acc.reward;
                            } else {
                                retired.emplace(stake_id);
                            }
                        }
                        for (const auto &stake_id: retired) {
                            part.erase(stake_id);
                        }
                    }});
                }
            });
        }
        if (_params.protocol_ver.keep_pointers()) {
            for (const auto &[stake_ptr, coin]: _stake_pointers) {
                if (const auto ptr_it = _ptr_to_stake.find(stake_ptr); ptr_it != _ptr_to_stake.end()) {
                    //logger::debug("rotate_snapshots epoch: {} pointer {} adds: {} to stake_id: {}",
                    //  _epoch, stake_ptr, cardano::amount { coin }, ptr_it->second);
                    auto &acc = _accounts.at(ptr_it->second);
                    acc.mark_stake += coin;
                    if (acc.mark_deleg)
                        _mark.pool_dist.add(*acc.mark_deleg, coin);
                }
            }
        }
    }

    void state::start_epoch(std::optional<uint64_t> new_epoch)
    {
        logger::debug("state::start_epoch: prev_epoch: {} new_epoch: {}", _epoch, new_epoch);
        run_pulser_if_ready();
        if (!new_epoch) {
            // increment the epoch only if seen some data
            if (_end_offset)
                new_epoch = _epoch + 1;
            else
                new_epoch = 0;
        }
        if (*new_epoch < _epoch || *new_epoch > _epoch + 1) [[unlikely]]
            throw error(fmt::format("unexpected new epoch value: {} the current epoch: {}", *new_epoch, _epoch));;
        _epoch = *new_epoch;
        _epoch_slot = 0;
        _apply_future_shelley_delegs(cardano::slot::from_epoch(_epoch, _cfg));
        _reset_shelley_delegs_schedule();
        const auto prev_params = _apply_param_updates();
        if (_params.protocol_ver.major >= 2) {
            {
                const auto delta_ireserves = _transfer_instant_rewards(_instant_rewards_reserves);
                _reserves -= delta_ireserves;
                const auto delta_itreasury = _transfer_instant_rewards(_instant_rewards_treasury);
                _treasury -= delta_itreasury;
                logger::debug("delta_ireserves: {} delta_itreasury: {}", delta_ireserves, delta_itreasury);
            }
            _transfer_potential_rewards(prev_params);
            rotate_snapshots();
            _prep_op_stake_dist();
            _apply_future_pool_params();
            _nonmyopic = std::move(_nonmyopic_next);
            _nonmyopic_reward_pot = _reward_pot;
            _reserves -= _delta_reserves;
            _delta_reserves = 0;
            _treasury += _delta_treasury;
            _delta_treasury = 0;
            _reward_pot = 0;
            _rewards_ready = false;
            _blocks_past_voting_deadline = 0;
            _clean_old_epoch_data();
            _fees_utxo -= _delta_fees;
            _delta_fees = _fees_next_reward;
            _fees_next_reward = 0;
            _pulsing_snapshot_slot = cardano::slot::from_epoch(_epoch, _cfg) + _cfg.shelley_randomness_stabilization_window;
            const auto [refunds_user, refunds_treasury] = _retire_pools();
            logger::debug("epoch {} start: treasury: {} reserves: {} user refunds: {} treasury refunds: {}",
                _epoch, cardano::amount { _treasury }, cardano::amount { _reserves },
                cardano::amount { refunds_user }, cardano::amount { refunds_treasury });
        }
    }

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

    void state::process_block(const uint64_t end_offset, const uint64_t slot)
    {
        if (end_offset > _end_offset)
            _end_offset = end_offset;
        if (_params.protocol_ver.major >= 2) {
            const auto epoch_slot = cardano::slot { slot, _cfg }.epoch_slot();
            if (epoch_slot >= _cfg.shelley_voting_deadline)
                ++_blocks_past_voting_deadline;
            if (epoch_slot > _epoch_slot)
                _epoch_slot = epoch_slot;
        }
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

    uint64_t state::_retire_avvm_balance()
    {
        std::atomic_uint64_t total_balance = 0;
        static const std::string task_group { "validator::state::retire_avvm_balance" };
        _sched.wait_all(task_group, [&](const auto &, const auto &submit_f) {
            for (size_t pi = 0; pi < _utxo.num_parts; ++pi) {
                submit_f({ 1000, task_group, [&, pi] {
                    uint64_t part_balance = 0;
                    auto &part = _utxo.partition(pi);
                    for (auto it = part.begin(), end = part.end(); it != end; ) {
                        const auto &txo_data = it->second;
                        if (txo_data.address_raw.at(0) == 0x82) {
                            auto crc_v = cbor::zero2::parse(txo_data.address_raw);
                            auto &crc_v_it = crc_v.get().array();
                            auto &addr_v_tag = crc_v_it.read();
                            auto addr_v = cbor::zero2::parse(addr_v_tag.tag().read().bytes());
                            auto &addr_v_it = addr_v.get().array();
                            addr_v_it.skip(2);
                            if (addr_v_it.read().uint() == 2) {
                                part_balance += txo_data.coin;
                                it = part.erase(it);
                                continue;
                            }
                        }
                        ++it;
                    }
                    total_balance.fetch_add(part_balance, std::memory_order_relaxed);
                }});
            }
        });
        return total_balance.load(std::memory_order_relaxed);
    }

    std::optional<cardano::stake_ident> state::_extract_stake_id(const cardano::address &addr) const
    {
        if (addr.has_stake_id()) [[likely]]
            return addr.stake_id();
        if (addr.has_pointer()) [[unlikely]] {
            const auto stake_ptr = addr.pointer();
            if (const auto ptr_it = _ptr_to_stake.find(stake_ptr); ptr_it != _ptr_to_stake.end())
                return ptr_it->second;
            logger::warn("epoch: {} an unrecognized stake pointer has been referenced {} - ignoring it", _epoch, stake_ptr);
        }
        return {};
    }

    void state::_prep_op_stake_dist()
    {
        _operating_stake_dist.clear();
        _operating_stake_dist.total_stake = _set.pool_dist.total_stake();
        for (const auto &[pool_id, coin]: _set.pool_dist) {
            if (_set.delegated_pools.contains(pool_id)) {
                const auto &params = _set.pool_params.at(pool_id).params;
                rational_u64 rel_stake { coin, _set.pool_dist.total_stake() };
                rel_stake.normalize();
                _operating_stake_dist.try_emplace(pool_id, std::move(rel_stake), coin, params.vrf_vkey);
            }
        }
    }

    void state::_apply_future_pool_params()
    {
        for (auto &&[pool_id, params]: _future_pool_params) {
            auto &active = _active_pool_params.at(pool_id);
            if (_params.protocol_ver.major >= 11)
                _remove_pool_vrf_key_hash(active.params.vrf_vkey);
            active = std::move(params);
        }
        _future_pool_params.clear();
    }

    void state::_add_pool_vrf_key_hash(const vrf_vkey &vrf)
    {
        auto [it, created] = _pool_vrf_key_hashes.try_emplace(vrf, 1);
        if (!created && it->second != std::numeric_limits<uint64_t>::max())
            ++it->second;
    }

    void state::_remove_pool_vrf_key_hash(const vrf_vkey &vrf)
    {
        const auto it = _pool_vrf_key_hashes.find(vrf);
        if (it == _pool_vrf_key_hashes.end()) [[unlikely]]
            throw error(fmt::format("VRF key {} is missing from the registered-key occurrence map", vrf));
        if (it->second <= 1)
            _pool_vrf_key_hashes.erase(it);
        else
            --it->second;
    }

    void state::_populate_pool_vrf_key_hashes()
    {
        _pool_vrf_key_hashes.clear();
        const auto add = [&](const auto &pools) {
            for (const auto &[pool_id, info]: pools) {
                static_cast<void>(pool_id);
                _add_pool_vrf_key_hash(info.params.vrf_vkey);
            }
        };
        add(_active_pool_params);
        add(_future_pool_params);
    }

    void state::_recompute_caches() const
    {
        _pbft_pools = _make_pbft_pools(_shelley_delegs);
    }

    uint64_t state::_total_stake(uint64_t reserves) const
    {
        return _cfg.shelley_max_lovelace_supply - reserves;
    }

    void state::_compute_rewards()
    {
        timer t { fmt::format("compute rewards for epoch {}", _epoch), logger::level::debug };
        _rewards_ready = true;
        uint64_t expansion = 0;
        if (rational_from_r64(_params_prev.decentralization) < rational_from_r64(_params_prev.decentralizationThreshold) && _epoch > 0) {
            cpp_rational perf = std::min(cpp_rational { 1 }, cpp_rational { _blocks_before.total_stake() } / ((1 - rational_from_r64(_params_prev.decentralization)) * _cfg.shelley_epoch_blocks));
            expansion = static_cast<uint64_t>(rational_from_r64(_params_prev.expansion_rate) * _reserves * perf);
            logger::trace("epoch: {} performance-adjusted expansion: {} perf: {} d: {} blocks: {}",
                _epoch, expansion, perf, _params_prev.decentralization, _blocks_before.total_stake());
        } else {
            expansion = static_cast<uint64_t>(rational_from_r64(_params_prev.expansion_rate) * _reserves);
            logger::trace("epoch: {} simple expansion: {}", _epoch, expansion);
        }
        const uint64_t total_reward_pool = expansion + _delta_fees;
        const uint64_t treasury_rewards = static_cast<uint64_t>(rational_from_r64(_params_prev.treasury_growth_rate) * total_reward_pool);
        _reward_pot = total_reward_pool - treasury_rewards;
        uint64_t pool_rewards_filtered = 0;
        const uint64_t total_stake = _total_stake(_reserves);
        if (!_blocks_before.empty()) {
            const auto &pools_active = _blocks_before;
            {
                timer t2 { fmt::format("compute per-pool rewards for epoch {}", _epoch), logger::level::trace };
                pool_rewards_filtered = _compute_pool_rewards_parallel(pools_active, _reward_pot, total_stake);
            }
            logger::trace("epoch {} total stake {} treasury: {} reserves: {} rewards pot: {} block-producing pools: {} reward pools: {} rewards attributed: {}",
                _epoch, total_stake, _treasury, _reserves, _reward_pot, pools_active.size(), _go.pool_params.size(), pool_rewards_filtered);
        }
        _delta_treasury = treasury_rewards;
        _delta_reserves = treasury_rewards + pool_rewards_filtered - _delta_fees;
        logger::debug("epoch {} deltaR ({}) = deltaT ({}) + poolRewards ({}) - deltaF {}",
            _epoch, cardano::amount { _delta_reserves }, cardano::amount { _delta_treasury },
            cardano::amount { pool_rewards_filtered }, cardano::amount { _delta_fees });
    }

    void state::_rewards_prepare_pool_params(uint64_t &total, uint64_t &filtered, const rational_u64 &z0,
        const uint64_t staking_reward_pot, const uint64_t total_stake, const pool_hash &pool_id,
        pool_info &info, const uint64_t pool_blocks)
    {
        const uint64_t pool_stake = _go.pool_dist.get(pool_id);
        uint64_t pool_reward_pot = 0;
        if (pool_stake > 0) {
            uint64_t leader_reward = 0;
            uint64_t owner_stake = 0;
            for (const auto &stake_id: info.params.owners) {
                if (const auto acc_it = _accounts.find(stake_id); acc_it != _accounts.end() && acc_it->second.go_deleg == pool_id)
                    owner_stake += acc_it->second.go_stake;
            }
            if (owner_stake >= info.params.pledge) {
                const cpp_rational z0_r = rational_from_r64(z0);
                const cpp_rational pool_rel_total_stake { pool_stake, std::max<uint64_t>(1, total_stake) };
                const cpp_rational sigma_mark = std::min(pool_rel_total_stake, z0_r);
                const cpp_rational pool_rel_active_stake { pool_stake, std::max<uint64_t>(1, _go.pool_dist.total_stake()) };
                const cpp_rational pledge_rel_total_stake { info.params.pledge, std::max<uint64_t>(1, total_stake) };
                if (pool_rel_total_stake < pledge_rel_total_stake) [[unlikely]]
                    throw error(fmt::format("internal error: pledged stake: {} of pool {} is larger than the pool's total stake: {}", info.params.pledge, pool_id, pool_stake));
                const cpp_rational s_mark = std::min(pledge_rel_total_stake, z0_r);
                const cpp_rational pool_pledge_influence = rational_from_r64(_params_prev.pool_pledge_influence);
                const cpp_rational factor1 = cpp_rational { staking_reward_pot } / (cpp_rational { 1 } + pool_pledge_influence);
                const cpp_rational factor4 = (z0_r - sigma_mark) / z0_r;
                const cpp_rational factor3 = (sigma_mark - s_mark * factor4) / z0_r;
                const cpp_rational factor2 = sigma_mark + s_mark * pool_pledge_influence * factor3;
                const uint64_t optimal_reward = static_cast<uint64_t>(factor1 * factor2);
                pool_reward_pot = optimal_reward;
                if (rational_from_r64(_params_prev.decentralization) < rational_from_r64(_params_prev.decentralizationThreshold)) {
                    const cpp_rational beta { pool_blocks, std::max<uint64_t>(1, _blocks_before.total_stake()) };
                    const cpp_rational pool_performance = pool_rel_active_stake != cpp_rational {} ? beta / pool_rel_active_stake : cpp_rational {};
                    pool_reward_pot = static_cast<uint64_t>(pool_performance * cpp_rational { optimal_reward });
                }
                if (pool_reward_pot > info.params.cost && owner_stake < pool_stake) {
                    cpp_rational &base = rational_from_storage(info.reward_base);
                    base = pool_reward_pot - info.params.cost;
                    base /= pool_stake;
                    base *= info.params.margin.denominator - info.params.margin.numerator;
                    base /= info.params.margin.denominator;
                    const cpp_rational pool_margin = rational_from_r64(info.params.margin);
                    const cpp_rational leader_reward_ratio = pool_margin
                        + (cpp_rational { 1 } - pool_margin) * cpp_rational { owner_stake, pool_stake };
                    leader_reward = info.params.cost + static_cast<uint64_t>(cpp_rational { pool_reward_pot - info.params.cost } * leader_reward_ratio);
                } else {
                    leader_reward = pool_reward_pot;
                }
            }
            const stake_ident reward_stake_id = info.params.reward_id;
            const bool leader_active = _params_prev.protocol_ver.forgo_reward_prefilter() || _reward_pulsing_snapshot.contains(reward_stake_id);
            if (leader_active) {
                auto &reward_list = _potential_rewards[reward_stake_id];
                total += leader_reward;
                if (!reward_list.empty())
                    filtered -= reward_list.begin()->amount;
                if (const auto acc_it = _accounts.find(reward_stake_id); acc_it != _accounts.end() && acc_it->second.deleg)
                    reward_list.emplace(reward_type::leader, pool_id, leader_reward, *acc_it->second.deleg);
                else
                    reward_list.emplace(reward_type::leader, pool_id, leader_reward);
                filtered += reward_list.begin()->amount;
            }
        }
    }

    std::pair<uint64_t, uint64_t> state::_rewards_prepare_pools(const pool_block_dist &pools_active, const uint64_t staking_reward_pot, const uint64_t total_stake)
    {
        uint64_t total = 0;
        uint64_t filtered = 0;
        const rational_u64 z0 { 1, _params_prev.n_opt };
        const cpp_rational z0_r = rational_from_r64(z0);
        _nonmyopic_next.clear();
        for (auto &[pool_id, pool_info]: _go.pool_params) {
            if (!_pbft_pools.contains(pool_id)) {
                const uint64_t pool_blocks = pools_active.get(pool_id);
                rational_from_storage(pool_info.reward_base) = cpp_rational {};
                if (pool_blocks > 0)
                    _rewards_prepare_pool_params(total, filtered, z0, staking_reward_pot, total_stake, pool_id, pool_info, pool_blocks);
                const cpp_rational rel_stake { _go.pool_dist.get(pool_id), total_stake };
                const auto rel_stake_bounded = std::min(z0_r, rel_stake);
                //logger::debug("estimating hit-rate likelihood epoch: {} pool: {} blocks: {} d: {} rel_stake: {} rel_stake_bounded: {}", _epoch, pool_id, pool_blocks, _params_prev.decentralization, rel_stake, rel_stake_bounded);
                pool_rank::likelihood_prior prior {};
                if (const auto prior_it = _nonmyopic.find(pool_id); prior_it != _nonmyopic.end())
                    prior.emplace(prior_it->second);
                _nonmyopic_next.try_emplace(pool_id, pool_rank::likelihoods(pool_blocks, _cfg.shelley_epoch_length,
                    static_cast<double>(rel_stake), _cfg.shelley_active_slots, _params_prev.decentralization, prior));
            }
        }
        return std::make_pair(total, filtered);
    }

    std::pair<uint64_t, uint64_t> state::_rewards_compute_part(const size_t part_idx)
    {
        uint64_t total = 0;
        uint64_t filtered = 0;
        auto &part = _potential_rewards.partition(part_idx);
        const auto &acc_part = _accounts.partition(part_idx);
        for (const auto &[stake_id, acc]: acc_part) {
            if (acc.go_deleg) {
                const auto &pool_info = _go.pool_params.at(*acc.go_deleg);
                if (std::find(pool_info.params.owners.begin(), pool_info.params.owners.end(), stake_id) == pool_info.params.owners.end()) {
                    const uint64_t deleg_stake = acc.go_stake;
                    const auto &reward_base = rational_from_storage(pool_info.reward_base);
                    const uint64_t member_reward = static_cast<uint64_t>(reward_base * deleg_stake);
                    if (member_reward > 0) {
                        const bool active = _params_prev.protocol_ver.forgo_reward_prefilter() || _reward_pulsing_snapshot.contains(stake_id);
                        if (active) {
                            auto &reward_list = part[stake_id];
                            total += member_reward;
                            if (!reward_list.empty())
                                filtered -= reward_list.begin()->amount;
                            if (acc.deleg)
                                reward_list.emplace(reward_type::member, *acc.go_deleg, member_reward, *acc.deleg);
                            else
                                reward_list.emplace(reward_type::member, *acc.go_deleg, member_reward);
                            filtered += reward_list.begin()->amount;
                        }
                    }
                }
            }
        }
        return std::make_pair(total, filtered);
    }

    uint64_t state::_compute_pool_rewards_parallel(const pool_block_dist &pools_active, const uint64_t staking_reward_pot, const uint64_t total_stake)
    {
        const std::string task_group = fmt::format("ledger-state:compute-rewards:epoch-{}", _epoch);
        const auto [init_total, init_filtered] = _rewards_prepare_pools(pools_active, staking_reward_pot, total_stake);
        std::atomic_uint64_t total = init_total;
        std::atomic_uint64_t filtered = init_filtered;
        _sched.wait_all(task_group,
            [&](const auto &, const auto &submit_f) {
                for (size_t part_idx = 0; part_idx < _potential_rewards.num_parts; ++part_idx) {
                    submit_f({1000, task_group, [&total, &filtered, this, part_idx] {
                        const auto [part_total, part_filtered] = _rewards_compute_part(part_idx);
                        total.fetch_add(part_total, std::memory_order_relaxed);
                        filtered.fetch_add(part_filtered, std::memory_order_relaxed);
                    }});
                }
            });
        logger::trace("epoch: {} staking_rewards total: {} filtered: {} diff: {}",
            _epoch, cardano::amount { total.load() }, cardano::amount { filtered.load() },
            cardano::balance_change { static_cast<int64_t>(filtered) - static_cast<int64_t>(total) });
        if (_params_prev.protocol_ver.aggregated_rewards())
            return total;
        return filtered;
    }

    void state::_clean_old_epoch_data()
    {
        const auto potential_rewards_size = _potential_rewards.size();
        timer t { fmt::format("validator::state epoch: {} clean_old_epoch_data potential_rewards: {}", _epoch, potential_rewards_size), logger::level::trace };
        _blocks_before = std::move(_blocks_current);
        _blocks_current.clear();
        _reward_pulsing_snapshot.clear();
        if (potential_rewards_size) {
            const std::string task_group = fmt::format("ledger-state:clean-potential-rewards:epoch-{}", _epoch);
            _sched.wait_all(task_group,
                [&](const auto &, const auto &submit_f) {
                    for (size_t part_idx = 0; part_idx < _potential_rewards.num_parts; ++part_idx) {
                        submit_f({1000, task_group, [this, part_idx] {
                            _potential_rewards.partition(part_idx).clear();
                        }});
                    }
                });
        }
    }

    void state::_apply_param_update(const param_update &update)
    {
        if (update.protocol_ver) {
            if (update.protocol_ver->major >= 2 && _params.protocol_ver.major < 2) {
                {
                    const auto utxo_bal = utxo_balance();
                    if (utxo_bal > _cfg.shelley_max_lovelace_supply) [[unlikely]]
                        throw error(fmt::format("utxo balance: {} is larger than the total ADA supply: {}",
                            cardano::amount { utxo_bal }, cardano::amount { _cfg.shelley_max_lovelace_supply }));
                    _reserves = _cfg.shelley_max_lovelace_supply - utxo_bal;
                }
                // remove empty UTXO entries
                static const std::string task_name { "shelley-remove-empty-utxos" };
                _sched.wait_all(task_name,
                    [&](const auto &, const auto &submit_f) {
                        for (size_t part_idx = 0; part_idx < txo_map::num_parts; ++part_idx) {
                            submit_f({ 1000, task_name, [this, part_idx] {
                                auto &utxo_part = _utxo.partition(part_idx);
                                for (auto it = utxo_part.begin(); it != utxo_part.end();) {
                                    if (it->second.coin) [[likely]] {
                                        ++it;
                                    } else {
                                        logger::debug("removed empty UTXO {}", it->first);
                                        it = utxo_part.erase(it);
                                    }
                                }
                            }});
                        }
                    });
                _apply_shelley_params(_params);
                _apply_shelley_params(_params_prev);
                _params_prev.protocol_ver = *update.protocol_ver;
            }
            if (update.protocol_ver->major >= 3 &&  _params.protocol_ver.major < 3) {
                const auto unspent_avvm = _retire_avvm_balance();
                _reserves += unspent_avvm;
                logger::info("retired {} in unclaimed AVVM vouchers", cardano::amount { unspent_avvm });
            }
        }
        const auto update_desc = _params.apply(update);
        logger::info("epoch: {} protocol params update: [ {}]", _epoch, update_desc);
    }

    std::optional<param_update> state::_prep_param_update() const
    {
        std::optional<param_update> update {};
        {
            std::unordered_map<param_update, size_t> votes {};
            for (const auto &[pool_id, proposal]: _ppups) {
                ++votes[proposal];
            }
            for (const auto &[prop, num_votes]: votes) {
                if (num_votes >= _cfg.shelley_update_quorum) {
                    if (update) [[unlikely]]
                        throw error("more than one protocol parameter update has a quorum!");
                    update.emplace(prop);
                } else {
                    logger::warn("update proposal with insufficient votes: {}: {}", num_votes, prop);
                }
            }
        }
        return update;
    }

    protocol_params state::_apply_param_updates()
    {
        auto orig_params_prev = std::move(_params_prev);
        _params_prev = _params;
        if (const auto update = _prep_param_update(); update)
            _apply_param_update(*update);
        _ppups = std::move(_ppups_future);
        _ppups_future.clear();
        return orig_params_prev;
    }

    void state::_tick(const uint64_t slot)
    {
        _apply_future_shelley_delegs(slot);
        if (_params.protocol_ver.major >= 2)
            _ensure_reward_pulsing_snapshot(slot);
    }

    void state::_ensure_reward_pulsing_snapshot(const uint64_t slot)
    {
        if (!_params_prev.protocol_ver.forgo_reward_prefilter() && slot > _pulsing_snapshot_slot
                && _reward_pulsing_snapshot.empty() && !_accounts.empty()) {
            timer t { fmt::format("epoch: {} create a pulsing snapshot of reward accounts", _epoch), logger::level::debug };
            _reward_pulsing_snapshot.reserve(_accounts.size());
            for (const auto &[stake_id, acc]: _accounts) {
                if (acc.ptr)
                    _reward_pulsing_snapshot.emplace_back(stake_id, acc.reward);
            }
        }
    }

    void state::_transfer_potential_rewards(const cardano::protocol_params &params_prev)
    {
        const auto aggregated = params_prev.protocol_ver.aggregated_rewards();
        const auto forgo_prefilter = params_prev.protocol_ver.forgo_reward_prefilter();
        const bool force_active = !aggregated || forgo_prefilter;
        static constexpr uint64_t diagnostic_reward_threshold = 1'000'000'000'000;
        timer t { fmt::format("validator::state epoch: {} transfer_potential_rewards aggregated forgo_prefilter: {}", _epoch, forgo_prefilter), logger::level::debug };
        using pool_index_map = std::unordered_map<cardano::pool_hash, size_t>;
        const std::string task_group = fmt::format("ledger-state:transfer-rewards:epoch-{}", _epoch);
        const auto num_active_pools = _active_pool_dist.size();
        pool_index_map active_pool_indices {};
        std::vector<uint64_t> part_pool_updates {};
        active_pool_indices.reserve(num_active_pools);
        size_t active_pool_idx = 0;
        for (const auto &[pool_id, stake]: _active_pool_dist) {
            static_cast<void>(stake);
            active_pool_indices.try_emplace(pool_id, active_pool_idx++);
        }
        part_pool_updates.resize(_potential_rewards.num_parts * num_active_pools);
        const pool_index_map &pool_indices = active_pool_indices;
        std::array<uint64_t, partitioned_reward_update_dist::num_parts> treasury_updates {};
        // all rewards must be already created to ensure no allocation is necessary
        _sched.wait_all(task_group,
            [&](const auto &, const auto &submit_f) {
                for (size_t part_idx = 0; part_idx < _potential_rewards.num_parts; ++part_idx) {
                    submit_f({ 1000, task_group, [this, &pool_indices, &part_pool_updates, &treasury_updates,
                            num_active_pools, part_idx, aggregated, force_active] {
                        partitioned_reward_update_dist::partition_type reward_part {};
                        reward_part.swap(_potential_rewards.partition(part_idx));
                        // relies on reward_part, _accounts, and _pulsing_snapshot being ordered containers!
                        auto acc_it = _accounts.partition(part_idx).begin();
                        const auto acc_end = _accounts.partition(part_idx).end();
                        const auto pool_update_offset = part_idx * num_active_pools;
                        uint64_t part_treasury_update = 0;
                        for (const auto &[stake_id, reward_list]: reward_part) {
                            uint64_t total_reward = 0;
                            for (const auto &ri: reward_list)
                                total_reward += ri.amount;
                            const bool eligible = force_active || _reward_pulsing_snapshot.contains(stake_id);
                            if (eligible) {
                                while (acc_it != acc_end && acc_it->first < stake_id)
                                    ++acc_it;
                                if (total_reward >= diagnostic_reward_threshold) {
                                    logger::debug("epoch: {} potential_rewards stake_id: {} total: {} account_known: {} registered: {} prev_reward: {} delegated: {} items: {}",
                                        _epoch, stake_id, cardano::amount { total_reward },
                                        acc_it != acc_end && acc_it->first == stake_id,
                                        acc_it != acc_end && acc_it->first == stake_id && acc_it->second.ptr.has_value(),
                                        cardano::amount { acc_it != acc_end && acc_it->first == stake_id ? acc_it->second.reward : 0 },
                                        acc_it != acc_end && acc_it->first == stake_id && acc_it->second.deleg.has_value(),
                                        reward_list.size());
                                }
                                const bool to_reward_account = acc_it != acc_end && acc_it->first == stake_id && acc_it->second.ptr;
                                uint64_t account_reward = 0;
                                for (auto &&ri: reward_list) {
                                    if (ri.amount) {
                                        if (total_reward >= diagnostic_reward_threshold || ri.amount >= diagnostic_reward_threshold) {
                                            logger::debug("epoch: {} potential_reward_transfer stake_id: {} type: {} pool_id: {} amount: {} destination: {} delegated_pool_id: {}",
                                                _epoch, stake_id, ri.type, ri.pool_id, cardano::amount { ri.amount },
                                                to_reward_account ? "reward-account" : "treasury", ri.delegated_pool_id);
                                        }
                                        if (to_reward_account)
                                            account_reward += ri.amount;
                                        else
                                            part_treasury_update += ri.amount;
                                        if (!aggregated)
                                            break;
                                    }
                                }
                                if (account_reward) {
                                    acc_it->second.reward += account_reward;
                                    // Rewards become part of the stake snapshot at the epoch boundary,
                                    // so they must follow the account's delegation at that boundary. The
                                    // delegation captured while calculating the reward can be stale after
                                    // a late-epoch redelegation.
                                    if (acc_it->second.deleg
                                            && _active_pool_params.contains(*acc_it->second.deleg)) {
                                        const auto pool_it = pool_indices.find(*acc_it->second.deleg);
                                        if (pool_it == pool_indices.end()) [[unlikely]] {
                                            throw error(fmt::format(
                                                "active reward delegation pool {} is missing from the stake distribution",
                                                *acc_it->second.deleg));
                                        }
                                        part_pool_updates[pool_update_offset + pool_it->second] += account_reward;
                                    }
                                }
                            } else if (total_reward >= diagnostic_reward_threshold) {
                                logger::debug("epoch: {} potential_rewards stake_id: {} total: {} skipped inactive account",
                                    _epoch, stake_id, cardano::amount { total_reward });
                            }
                        }
                        treasury_updates[part_idx] = part_treasury_update;
                    }});
                }
            });
        uint64_t treasury_update = 0;
        for (const auto amount: treasury_updates)
            treasury_update += amount;
        logger::debug("epoch {} transfer_potential_rewards treasury_update: {}", _epoch, treasury_update);
        _treasury += treasury_update;
        {
            std::vector<uint64_t> pool_updates(num_active_pools, 0);
            for (size_t part_idx = 0; part_idx < _potential_rewards.num_parts; ++part_idx) {
                const auto pool_update_offset = part_idx * num_active_pools;
                for (size_t pool_idx = 0; pool_idx < num_active_pools; ++pool_idx)
                    pool_updates[pool_idx] += part_pool_updates[pool_update_offset + pool_idx];
            }
            size_t pool_idx = 0;
            for (auto pool_it = _active_pool_dist.begin(); pool_it != _active_pool_dist.end(); ++pool_it)
                _active_pool_dist.add(pool_it, pool_updates[pool_idx++]);
        }
    }

    uint64_t state::_transfer_instant_rewards(stake_distribution &rewards)
    {
        timer t { fmt::format("validator::state epoch: {} transfer_instant_rewards", _epoch) };
        uint64_t sum = 0;
        for (const auto &[stake_id, reward]: rewards) {
            if (auto acc_it = _accounts.find(stake_id); acc_it != _accounts.end() && acc_it->second.ptr) {
                sum += reward;
                acc_it->second.reward += reward;
                if (acc_it->second.deleg)
                    _active_pool_dist.add(*acc_it->second.deleg, reward);
            }
        }
        rewards.clear();
        return sum;
    }

    std::pair<uint64_t, uint64_t> state::_retire_pools()
    {
        uint64_t refunds_user = 0;
        uint64_t refunds_treasury = 0;
        for (auto it = _pools_retiring.begin(); it != _pools_retiring.end(); ) {
            if (_epoch >= it->second) {
                const auto &pool_id = it->first;
                const auto &pool_info = _active_pool_params.at(pool_id);
                const auto pd_it = _pool_deposits.find(pool_id);
                if (pd_it == _pool_deposits.end()) [[unlikely]]
                    throw error("retiring pool {} does not have a deposit record!");
                const auto pool_deposit = pd_it->second;
                _pool_deposits.erase(pd_it);
                //logger::trace("epoch: {} returning the deposit of a retiring pool {} to {}", _epoch, it->first, pool_info.reward_id);
                for (const auto &stake_id: _active_inv_delegs.at(pool_id))
                    _accounts.at(stake_id).deleg.reset();
                _active_inv_delegs.erase(pool_id);
                const stake_ident reward_stake_id = pool_info.params.reward_id;
                if (auto acc_it = _accounts.find(reward_stake_id); acc_it != _accounts.end() && acc_it->second.ptr) {
                    acc_it->second.reward += pool_deposit;
                    if (const auto rew_acc_it = _accounts.find(reward_stake_id); rew_acc_it != _accounts.end() && rew_acc_it->second.deleg) {
                        if (_active_pool_params.contains(*rew_acc_it->second.deleg)) {
                            _active_pool_dist.add(*rew_acc_it->second.deleg, pool_deposit);
                            refunds_user += pool_deposit;
                        }
                    }
                } else {
                    logger::trace("epoch: {} can't return the deposit of a retiring pool {}, so it goes to the treasury", _epoch, it->first);
                    _treasury += pool_deposit;
                    refunds_treasury += pool_deposit;
                }

                if (_deposited < pool_deposit) [[unlikely]]
                    throw error("trying to remove a deposit while having insufficient deposits");
                _deposited -= pool_deposit;
                _active_pool_dist.retire(it->first);
                if (_params.protocol_ver.major >= 11)
                    _remove_pool_vrf_key_hash(pool_info.params.vrf_vkey);
                _active_pool_params.erase(pool_id);
                it = _pools_retiring.erase(it);
            } else {
                ++it;
            }
        }
        return std::make_pair(refunds_user, refunds_treasury);
    }

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
