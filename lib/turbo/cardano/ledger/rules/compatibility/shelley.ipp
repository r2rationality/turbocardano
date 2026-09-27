/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: shared historical compatibility; see doc/conway-agda-rule-map.md.
// Included at namespace scope by lib/turbo/cardano/ledger/shelley.cpp.

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
