/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <boost/beast/http/status.hpp>
#include <turbo/cardano/ledger/conway.hpp>
#include <turbo/cardano/ledger/updates.hpp>

namespace turbo::cardano::ledger::conway {
    template<typename M, typename K>
    auto map_nice_at(const M &map, const K &key) -> decltype(map.at(key))
    {
        const auto it = map.find(key);
        if (it == map.end()) [[unlikely]]
            throw error(fmt::format("unable to find key {} in map of type {}", key, typeid(M).name()));
        return it->second;
    }

    template<typename M, typename K>
    auto map_nice_at(M &map, const K &key) -> decltype(map.at(key))
    {
        return const_cast<decltype(map.at(key))>(map_nice_at(const_cast<const M &>(map), key));
    }

    vrf_state::vrf_state(babbage::vrf_state &&o): babbage::vrf_state { std::move(o) }
    {
        _max_epoch_slot = _cfg.shelley_epoch_length - _cfg.shelley_randomness_stabilization_window;
        logger::debug("conway::vrf_state created max_epoch_slot: {}", _max_epoch_slot);
    }

    void committee_t::hot_key_t::to_cbor(era_encoder &enc) const
    {
        std::visit([&](auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, credential_t>) {
                enc.array(2);
                enc.uint(0);
                v.to_cbor(enc);
            } else {
                throw error(fmt::format("unsupported hot_key_t value: {}", typeid(T).name()));
            }
        }, val);
    }

    size_t committee_t::active_size(const member_key_map &hot_keys) const
    {
        size_t sz = 0;
        for (const auto &[cold_id, hot_id]: hot_keys) {
            if (std::holds_alternative<credential_t>(hot_id.val))
                ++sz;
        }
        return sz;
    }

    uint64_t drep_info_t::compute_expire_epoch(const protocol_params &pp, const uint64_t current_epoch)
    {
        return current_epoch + pp.drep_activity;
    }

    void drep_info_t::to_cbor(era_encoder &enc) const
    {
        enc.array(4);
        enc.uint(expire_epoch);
        // ledger-state format is not compatible with the block format
        if (anchor) {
            enc.array(1);
            anchor->to_cbor(enc);
        } else {
            enc.array(0);
        }
        enc.uint(deposited);
        delegs.to_cbor(enc);
    }

    committee_t committee_t::from_json(const json::value &j)
    {
        member_map members {};
        for (const auto &[cred, epoch]: j.at("members").as_object()) {
            members.try_emplace(credential_t::from_json(cred), json::value_to<uint64_t>(epoch));
        }
        return {
            std::move(members),
            decltype(threshold)::from_json(j.at("threshold"))
        };
    }

    void committee_t::to_cbor(era_encoder &enc) const
    {
        enc.array(2);
        enc.map_compact(members.size(), [&] {
            for (const auto &[cred, epoch]: members) {
                cred.to_cbor(enc);
                enc.uint(epoch);
            }
        });
        threshold.to_cbor(enc);
    }

    void gov_action_state_t::to_cbor(era_encoder &enc, const gov_action_id_t &id) const
    {
        enc.array(7);
        id.to_cbor(enc);
        // committee votes
        {
            auto k_enc { enc }, s_enc { enc };
            size_t k_cnt = 0, s_cnt = 0;
            for (const auto &[cred, vote]: committee_votes) {
                auto &my_enc = cred.script ? s_enc : k_enc;
                auto &my_cnt = cred.script ? s_cnt : k_cnt;
                cred.to_cbor(my_enc);
                vote.to_cbor(my_enc);
                ++my_cnt;
            }
            enc.map_compact(k_cnt + s_cnt, [&] {
                enc << s_enc;
                enc << k_enc;
            });
        }
        {
            auto k_enc { enc }, s_enc { enc };
            size_t k_cnt = 0, s_cnt = 0;
            for (const auto &[cred, vote]: drep_votes) {
                auto &my_enc = cred.script ? s_enc : k_enc;
                auto &my_cnt = cred.script ? s_cnt : k_cnt;
                cred.to_cbor(my_enc);
                vote.to_cbor(my_enc);
                ++my_cnt;
            }
            enc.map_compact(k_cnt + s_cnt, [&] {
                enc << s_enc;
                enc << k_enc;
            });
        }
        {
            enc.map_compact(pool_votes.size(), [&] {
                for (const auto &[id, vote]: pool_votes) {
                    enc.bytes(id);
                    vote.to_cbor(enc);
                }
            });
        }
        proposal.to_cbor(enc);
        enc.uint(proposed_in);
        enc.uint(expires_after);
    }

    void prev_actions_t::to_cbor(era_encoder &enc) const
    {
        enc.array(4);
        param_updates.to_cbor(enc);
        hard_forks.to_cbor(enc);
        committee_updates.to_cbor(enc);
        committee_updates.to_cbor(enc);
    }

    void enact_state_t::to_cbor(era_encoder &enc) const
    {
        enc.array(7);
        committee.to_cbor(enc);
        constitution.to_cbor(enc);
        conway::protocol_params_to_cbor(enc, params);
        conway::protocol_params_to_cbor(enc, prev_params);
        enc.uint(treasury);
        enc.map_compact(withdrawals.size(), [&] {
            for (const auto &[stake_id, stake]: withdrawals) {
                stake_id.to_cbor(enc);
                enc.uint(stake);
            }
        });
        prev_actions.to_cbor(enc);
    }

    void state::ratify_state_t::to_cbor(era_encoder &enc) const
    {
        enc.array(4);
        new_state.to_cbor(enc);
        enc.array_compact(enacted.size(), [&] {
            for (const auto &[id, gas]: enacted) {
                gas.to_cbor(enc, id);
            }
        });
        expired.to_cbor(enc);
        if (delayed)
            enc.s_true();
        else
            enc.s_false();
    }

    void pulsing_data_t::from_zpp(parallel_decoder &dec)
    {
        dec.add([&](const auto b) {
            zpp::deserialize(proposals, b);
        });
        dec.add([&](const auto b) {
            zpp::deserialize(drep_state, b);
        });
        dec.add([&](const auto b) {
            zpp::deserialize(drep_voting_power, b);
        });
        dec.add([&](const auto b) {
            zpp::deserialize(pool_voting_power, b);
        });
        dec.add([&](const auto b) {
            zpp::deserialize(drep_state_updated, b);
        });
    }

    void pulsing_data_t::to_zpp(zpp_encoder &enc) const
    {
        enc.add([&](auto) {
            return zpp::serialize(proposals);
        });
        enc.add([&](auto) {
            return zpp::serialize(drep_state);
        });
        enc.add([&](auto) {
            return zpp::serialize(drep_voting_power);
        });
        enc.add([&](auto) {
            return zpp::serialize(pool_voting_power);
        });
        enc.add([&](auto) {
            return zpp::serialize(drep_state_updated);
        });
    }

    state::state(): state { babbage::state { shelley::state { cardano::config::get(), scheduler::get() } } }
    {
        const protocol_version pv { 9, 0 };
        _params.protocol_ver = pv;
        _params_prev.protocol_ver = pv;
        _ratify_state.new_state.params.protocol_ver = pv;
    }

    state::state(babbage::state &&o):
        babbage::state { std::move(o) },
        _enact_state {
            committee_t::from_json(_cfg.conway_genesis.at("committee")),
            constitution_t::from_json(_cfg.conway_genesis.at("constitution"))
        }
    {
        _apply_conway_params(_params);
        _params_prev = _params;
        static const std::string task_name { "conway-update-utxos" };
        _sched.wait_all(task_name,
            [&](const auto &, const auto &submit_f) {
                for (size_t part_idx = 0; part_idx < txo_map::num_parts; ++part_idx) {
                    submit_f({1000, task_name, [this, part_idx] {
                        auto &utxo_part = _utxo.partition(part_idx);
                        for (auto &&[txo_id, txo_data]: utxo_part) {
                            if (const auto addr = txo_data.addr(); addr.has_pointer()) {
                                const auto ptr = addr.pointer();
                                if (ptr.slot > slot::from_epoch(_epoch, _cfg)
                                        || ptr.tx_idx >= std::numeric_limits<uint16_t>::max()
                                        || ptr.cert_idx >= std::numeric_limits<uint16_t>::max()) {
                                    const auto old_addr = txo_data.address_raw;
                                    txo_data.address_raw.resize(29);
                                    txo_data.address_raw << uint8_t { 0 } << uint8_t { 0 } << uint8_t { 0 };
                                    logger::debug("conway-start: txo_id: {} updated address {} => {}", txo_id, old_addr, txo_data.address_raw);
                                }
                            }
                        }
                    }});
                }
            }
        );

        _enact_state.params = _params;
        _enact_state.prev_params = _params_prev;
        _enact_state.treasury = 0;

        _ratify_state.new_state = _enact_state;

        _gov_make_pulsing_snapshot();
    }

    void state::process_cert(const cert_t &cert, const cert_loc_t &loc)
    {
        _tick(loc.slot);
        std::visit([&](const auto &c) {
            process_cert(c, loc);
        }, cert.val);
    }

    bool state::has_drep(const credential_t &id) const
    {
        return _drep_state.contains(id);
    }

    void state::_tick(const uint64_t slot)
    {
        babbage::state::_tick(slot);
        if (!_pulsing_data.drep_state_updated && slot > _pulsing_snapshot_slot) {
            logger::debug("slot: {} creating drep pulser snapshots", cardano::slot { slot, _cfg });
            _pulsing_data.drep_state_updated = true;
        }
    }

    void state::_add_encode_task(cbor_encoder &ser, const encode_cbor_func &t) const
    {
        ser.add([t](auto enc) {
            t(enc);
            return std::move(enc.cbor());
        });
    }

    void state::_apply_conway_params(protocol_params &p) const
    {
        const auto &co_cfg = _cfg.conway_genesis;
        p.plutus_cost_models.v3.emplace(plutus_cost_model::from_json(_cfg.plutus_all_cost_models.v3.value(), co_cfg.at("plutusV3CostModel")));
        p.pool_voting_thresholds = _cfg.conway_pool_voting_thresholds;
        p.drep_voting_thresholds = _cfg.conway_drep_voting_thresholds;
        p.committee_min_size = json::value_to<uint64_t>(co_cfg.at("committeeMinSize"));
        p.committee_max_term_length = json::value_to<uint64_t>(co_cfg.at("committeeMaxTermLength"));
        p.gov_action_lifetime = json::value_to<uint64_t>(co_cfg.at("govActionLifetime"));
        p.gov_action_deposit = json::value_to<uint64_t>(co_cfg.at("govActionDeposit"));
        p.drep_deposit = json::value_to<uint64_t>(co_cfg.at("dRepDeposit"));
        p.drep_activity = json::value_to<uint64_t>(co_cfg.at("dRepActivity"));
        p.min_fee_ref_script_cost_per_byte = decltype(p.min_fee_ref_script_cost_per_byte)::from_json(json::value_to<double>(co_cfg.at("minFeeRefScriptCostPerByte")));
    }

    void state::_donations_to_cbor(era_encoder &enc) const
    {
        enc.uint(_donations);
    }

    void state::_params_to_cbor(era_encoder &enc, const protocol_params &params) const
    {
        conway::protocol_params_to_cbor(enc, params);
    }

    void state::_protocol_state_to_cbor(era_encoder &enc) const
    {
        enc.array(7);
        // gov_action_state
        {
            enc.array(2);
            _enact_state.prev_actions.to_cbor(enc);
            proposal_map_copy proposals_copy {};
            proposals_copy.reserve(_proposals.size());
            for (const auto &[gid, gas]: _proposals)
                proposals_copy.emplace_back(gid, gas);
            std::sort(proposals_copy.begin(), proposals_copy.end(),[](const auto &l, const auto &r) {
                return l.second.loc < r.second.loc;
            });
            enc.array_compact(proposals_copy.size(), [&] {
                for (const auto &[id, action]: proposals_copy) {
                    action.to_cbor(enc, id);
                }
            });
        }
        _enact_state.committee.to_cbor(enc);
        _enact_state.constitution.to_cbor(enc);
        _params_to_cbor(enc, _params);
        _params_to_cbor(enc, _params_prev);
        // future params
        if (_ratify_state.new_state.params != _params) {
            enc.array(2);
            enc.uint(1);
            _params_to_cbor(enc, _ratify_state.new_state.params);
        } else {
            enc.array(1).uint(0);
        }
        // voting stats?
        {
            enc.array(2);
            {
                // DRep pulser state
                enc.array(4);
                {
                    auto proposals_copy = _pulsing_data.proposals;
                    std::sort(proposals_copy.begin(), proposals_copy.end(), [&](const auto &l, const auto &r) {
                        return l.second.loc < r.second.loc;
                    });
                    enc.array_compact(proposals_copy.size(), [&] {
                        for (const auto &[id, action]: proposals_copy) {
                            if (_epoch > action.proposed_in)
                                action.to_cbor(enc, id);
                        }
                    });
                }
                enc.map_compact(_pulsing_data.drep_voting_power.size(), [&] {
                    for (const auto &[drep, power]: _pulsing_data.drep_voting_power) {
                        drep.to_cbor(enc);
                        enc.uint(power);
                    }
                });
                {
                    auto k_enc { enc }, s_enc { enc };
                    size_t k_cnt = 0, s_cnt = 0;
                    for (auto &[drep_id, info]: _pulsing_data.drep_state) {
                        auto &my_enc = drep_id.script ? s_enc : k_enc;
                        auto &my_cnt = drep_id.script ? s_cnt : k_cnt;
                        drep_id.to_cbor(my_enc);
                        info.to_cbor(my_enc);
                        ++my_cnt;
                    }
                    enc.map_compact(k_cnt + s_cnt, [&] {
                        enc << s_enc;
                        enc << k_enc;
                    });
                }
                // spo voting power
                enc.map_compact( _pulsing_data.pool_voting_power.size(), [&] {
                    for (const auto &[pool_id, stake]: _pulsing_data.pool_voting_power) {
                        enc.bytes(pool_id);
                        enc.uint(stake);
                    }
                });
            }
            _ratify_state.to_cbor(enc);
        }
    }

    void state::_stake_pointers_to_cbor(era_encoder &enc) const
    {
        enc.map(0);
    }

    void state::_stake_pointer_stake_to_cbor(era_encoder &enc) const
    {
        enc.map(0);
    }

    void state::_account_to_cbor(const account_info &acc, era_encoder &enc) const
    {
        enc.array(4);
        enc.array(1)
            .array(2).uint(acc.reward).uint(acc.deposit);
        enc.tag(258).array(0);
        if (acc.deleg) {
            enc.array(1).bytes(*acc.deleg);
        } else {
            enc.array(0);
        }
        acc.vote_deleg.to_cbor(enc);
    }

    void state::_delegation_gov_to_cbor(era_encoder &enc) const
    {
        enc.array(3);
        enc.map_compact(_drep_state.size(), [&] {
            auto k_enc { enc };
            for (const auto &[drep_id, info]: _drep_state) {
                auto &my_enc = drep_id.script ? enc : k_enc;
                drep_id.to_cbor(my_enc);
                info.to_cbor(my_enc);
            }
            enc << k_enc;
        });
        enc.map_compact(_committee_hot_keys.size(), [&] {
            for (const auto &[cold_id, hot_id]: _committee_hot_keys) {
                cold_id.to_cbor(enc);
                hot_id.to_cbor(enc);
            }
        });
        enc.uint(0);
    }

    void state::delegate_vote(const stake_ident &stake_id, const drep_t &drep, const cert_loc_t &loc)
    {
        logger::debug("slot: {} delegate_vote stake_id: {} drep: {}", slot { loc.slot, _cfg }, stake_id, drep);
        const auto preserve_incorrect_delegation = _params.protocol_ver.bootstrap_phase();
        auto new_drep_it = _drep_state.end();
        if (std::holds_alternative<credential_t>(drep.val))
            new_drep_it = _drep_state.find(std::get<credential_t>(drep.val));
        auto &acc = map_nice_at(_accounts, stake_id);
        if (acc.vote_deleg && std::holds_alternative<credential_t>(acc.vote_deleg->val)) {
            const auto &old_cred = std::get<credential_t>(acc.vote_deleg->val);
            auto old_drep_it = _drep_state.find(old_cred);
            if (old_drep_it != _drep_state.end() && (!preserve_incorrect_delegation || new_drep_it == _drep_state.end())) {
                old_drep_it->second.delegs.erase(stake_id);
            }
        }
        // re-delegation can happen to the same drep so must emplace after the removal
        if (std::holds_alternative<credential_t>(drep.val)) {
            if (new_drep_it == _drep_state.end()) [[unlikely]] {
                if (!preserve_incorrect_delegation)
                    throw error(fmt::format("delegate_vote: {} delegating to an unknown drep credential: {}", stake_id, std::get<credential_t>(drep.val)));
                logger::debug("delegate_vote: {} to an unknown DRep {} - ignoring in protocol ver: {} ", stake_id, std::get<credential_t>(drep.val), _params.protocol_ver);
            } else {
                new_drep_it->second.delegs.emplace(stake_id);
            }
        }
        acc.vote_deleg = drep;
    }

    void state::retire_stake(const uint64_t slot, const stake_ident &stake_id, const std::optional<uint64_t> deposit)
    {
        logger::debug("slot: {} conway::retire_stake id: {} deposit: {}", cardano::slot { slot, _cfg }, stake_id, deposit);
        auto &acc = map_nice_at(_accounts, stake_id);
        if (acc.vote_deleg) {
            if (std::holds_alternative<credential_t>(acc.vote_deleg->val)) {
                auto d_it = _drep_state.find(std::get<credential_t>(acc.vote_deleg->val));
                if (d_it != _drep_state.end())
                    d_it->second.delegs.erase(stake_id);
            }
            acc.vote_deleg.reset();
        }
        babbage::state::retire_stake(slot, stake_id, deposit);
    }

    void state::process_cert(const reg_cert &c, const cert_loc_t &loc)
    {
        register_stake(loc.slot, c.stake_id, c.deposit, loc.tx_idx, loc.cert_idx);
    }

    void state::process_cert(const unreg_cert &c, const cert_loc_t &loc)
    {
        retire_stake(loc.slot, c.stake_id, c.deposit);
    }

    void state::process_cert(const vote_deleg_cert &c, const cert_loc_t &loc)
    {
        delegate_vote(c.stake_id, c.drep, loc);
    }

    void state::process_cert(const stake_vote_deleg_cert &c, const cert_loc_t &loc)
    {
        delegate_stake(c.stake_id, c.pool_id);
        delegate_vote(c.stake_id, c.drep, loc);
    }

    void state::process_cert(const stake_reg_deleg_cert &c, const cert_loc_t &loc)
    {
        register_stake(loc.slot, c.stake_id, c.deposit, loc.tx_idx, loc.cert_idx);
        delegate_stake(c.stake_id, c.pool_id);
    }

    void state::process_cert(const vote_reg_deleg_cert &c, const cert_loc_t &loc)
    {
        register_stake(loc.slot, c.stake_id, c.deposit, loc.tx_idx, loc.cert_idx);
        delegate_vote(c.stake_id, c.drep, loc);
    }

    void state::process_cert(const stake_vote_reg_deleg_cert &c, const cert_loc_t &loc)
    {
        register_stake(loc.slot, c.stake_id, c.deposit, loc.tx_idx, loc.cert_idx);
        delegate_stake(c.stake_id, c.pool_id);
        delegate_vote(c.stake_id, c.drep, loc);
    }

    void state::process_cert(const auth_committee_hot_cert &c, const cert_loc_t &)
    {
        logger::debug("epoch: {} cert auth_enact_state.committee_hot_cert cold_id: {} hot_id: {}", _epoch, c.cold_id, c.hot_id);
        if (_enact_state.committee) {
            // Do not check for the presence in the committee to allow new members to immediately update their certs
            const auto [it, created] = _committee_hot_keys.try_emplace(c.cold_id, c.hot_id);
            if (!created) {
                if (std::holds_alternative<committee_t::resigned_t>(it->second.val)) [[unlikely]]
                    throw error(fmt::format("an attempt to provide a hot certificate to a resigned committee member: {}", c.cold_id));
                it->second.val = c.hot_id;
            }
        }
    }

    void state::process_cert(const resign_committee_cold_cert &c, const cert_loc_t &)
    {
        logger::debug("epoch: {} cert resign_committee_cold_cert cold_id: {}", _epoch, c.cold_id);
        if (_enact_state.committee) {
            if (auto it = _committee_hot_keys.find(c.cold_id); it != _committee_hot_keys.end()) [[likely]] {
                it->second.val = committee_t::resigned_t {};
            } else {
                throw error(fmt::format("an unknown resigning committee cold_id: {}", c.cold_id));
            }
        }
    }

    void state::process_cert(const reg_drep_cert &c, const cert_loc_t &loc)
    {
        logger::debug("slot: {} reg_drep id: {} anchor: {}", slot { loc.slot, _cfg }, c.drep_id, c.anchor);
        const auto [it, created] = _drep_state.try_emplace(c.drep_id, c.deposit, c.anchor, drep_info_t::compute_expire_epoch(_params, _epoch));
        if (!created) [[unlikely]]
            throw error(fmt::format("drep already registered: {}", c.drep_id));
        _deposited += c.deposit;
    }

    void state::process_cert(const unreg_drep_cert &c, const cert_loc_t &loc)
    {
        logger::debug("slot: {} unreg_drep id: {}", slot { loc.slot, _cfg }, c.drep_id);
        const auto it = _drep_state.find(c.drep_id);
        if (it ==_drep_state.end()) [[unlikely]]
            throw error(fmt::format("unreg_drep_cert: an unknown drep_id: {}", c.drep_id));
        // due to a Cardano Node bug in protocol version 9.0 there can be delegates in the list that have already re-delegated
        for (const auto &deleg_id: it->second.delegs) {
            auto acc_it = _accounts.find(deleg_id);
            if (acc_it != _accounts.end() && acc_it->second.vote_deleg) {
                // do not check if the creds match to be compatible with Cardano Nde
                acc_it->second.vote_deleg.reset();
            }
        }
        for (auto &[id, ga_st]: _proposals) {
            ga_st.drep_votes.erase(c.drep_id);
        }
        if (it->second.deposited != c.deposit) [[unlikely]]
            throw error(fmt::format("the registered drep deposit: {} does not match the requested withdrawal: {}", it->second.deposited, c.deposit));
        if (_deposited < c.deposit) [[unlikely]]
            throw error(fmt::format("unable to withdraw the old drep deposit: {}", it->second.deposited));
        _deposited -= c.deposit;
        _drep_state.erase(it);
    }

    void state::process_cert(const update_drep_cert &c, const cert_loc_t &loc)
    {
        logger::debug("slot: {} update_drep id: {} anchor: {}", slot { loc.slot, _cfg }, c.drep_id, c.anchor);
        const auto drep_it = _drep_state.find(c.drep_id);
        if (drep_it ==_drep_state.end()) [[unlikely]]
            throw error(fmt::format("unreg_drep_cert: an unknown drep_id: {}", c.drep_id));
        drep_it->second.anchor = c.anchor;
        drep_it->second.expire_epoch = drep_info_t::compute_expire_epoch(_params, _epoch);
        ++drep_it->second.num_updates;
    }

    void state::process_proposal(const proposal_t &p, const cert_loc_t &loc)
    {
        logger::debug("slot: {} process_proposal: id: {}", slot { loc.slot, _cfg }, p.id);
        if (_proposals.empty()) {
            for (auto &[id, info]: _drep_state) {
                if (!info.num_updates)
                    info.expire_epoch = drep_info_t::compute_expire_epoch(_params, _epoch) + 1;
            }
        }
        _proposals.try_emplace(p.id, p.procedure, _epoch, _epoch + _params_prev.gov_action_lifetime, loc);
        _deposited += p.procedure.deposit;
    }

    void state::process_vote(const vote_info_t &v, const cert_loc_t &loc)
    {
        const slot loc_slot { loc.slot, _cfg };
        logger::debug("slot: {} process_vote: voter: {} gid: {} vote: {}", loc_slot, v.voter, v.action_id, v.voting_procedure);
        if (auto gov_it = _proposals.find(v.action_id); gov_it != _proposals.end()) [[unlikely]] {
            switch (v.voter.type) {
                case voter_t::type_t::const_comm_key:
                case voter_t::type_t::const_comm_script: {
                    if (_enact_state.committee) {
                        const credential_t hot_id { v.voter.hash, v.voter.type == voter_t::type_t::const_comm_script };
                        const auto it = std::find_if(_committee_hot_keys.begin(), _committee_hot_keys.end(), [&](const auto &item) {
                            return std::holds_alternative<credential_t>(item.second.val) && std::get<credential_t>(item.second.val) == hot_id &&_enact_state.committee->members.contains(item.first);
                        });
                        if (it == _committee_hot_keys.end()) [[unlikely]]
                            throw error(fmt::format("a vote from an unknown committee member with a hot_id: {} at {}", hot_id, loc_slot));
                        gov_it->second.committee_votes[hot_id] = v.voting_procedure;
                    } else {
                        logger::warn("an attempted committee vote with no active committee: {} at {}", v, loc_slot);
                    }
                    break;
                }
                case voter_t::type_t::drep_key:
                case voter_t::type_t::drep_script: {
                    const credential_t drep_id { v.voter.hash, v.voter.type == voter_t::type_t::drep_script };
                    auto it = _drep_state.find(drep_id);
                    if (it == _drep_state.end()) [[unlikely]]
                        throw error(fmt::format("a vote from an unknown drep: {} at {}", drep_id, loc_slot));
                    it->second.expire_epoch = drep_info_t::compute_expire_epoch(_params, _epoch);
                    gov_it->second.drep_votes[drep_id] = v.voting_procedure;
                    break;
                }
                case voter_t::type_t::pool_key: {
                    if (!_active_pool_params.contains(v.voter.hash)) [[unlikely]]
                        throw error(fmt::format("a vote from an unknown pool: {} at {}", v.voter.hash, loc_slot));
                    gov_it->second.pool_votes[v.voter.hash] = v.voting_procedure;
                    break;
                }
                default: throw error(fmt::format("an unsupported voter type: {} at {}", v.voter.type, loc_slot));
            }
        } else {
            logger::warn("a vote for an unknown gov_action_id: {} at {}", v.action_id, loc_slot);
        }
    }

    void state::_process_block_updates(block_update_list &&block_updates)
    {
        for (const auto &bu: block_updates)
            _donations += bu.donations;
        babbage::state::_process_block_updates(std::move(block_updates));
    }

    void state::_process_timed_update(tx_out_ref_list &collected_collateral, timed_update_t &&upd)
    {
        std::visit([&](const auto &u) {
            using T = std::decay_t<decltype(u)>;
            if constexpr (std::is_same_v<T, reg_cert>
                || std::is_same_v<T, stake_reg_deleg_cert>
                || std::is_same_v<T, vote_reg_deleg_cert>
                || std::is_same_v<T, stake_vote_reg_deleg_cert>
                || std::is_same_v<T, reg_drep_cert>
                || std::is_same_v<T, vote_deleg_cert>
                || std::is_same_v<T, stake_vote_deleg_cert>
                || std::is_same_v<T, auth_committee_hot_cert>
                || std::is_same_v<T, resign_committee_cold_cert>
                || std::is_same_v<T, update_drep_cert>
                || std::is_same_v<T, unreg_cert>
                || std::is_same_v<T, unreg_drep_cert>) {
                process_cert(u, upd.loc);
            } else if constexpr (std::is_same_v<T, proposal_t>) {
                process_proposal(u, upd.loc);
            } else if constexpr (std::is_same_v<T, vote_info_t>) {
                process_vote(u, upd.loc);
            } else {
                babbage::state::_process_timed_update(collected_collateral, std::move(upd));
            }
        }, upd.update);
    }

    void state::to_zpp(zpp_encoder &ser) const
    {
        ser.add([&](auto) {
            return zpp::serialize(_enact_state);
        });
        ser.add([&](auto) {
            return zpp::serialize(_ratify_state);
        });
        _pulsing_data.to_zpp(ser);
        ser.add([&](auto) {
            return zpp::serialize(_committee_hot_keys);
        });
        ser.add([&](auto) {
            return zpp::serialize(_drep_state);
        });
        ser.add([&](auto) {
            return zpp::serialize(_num_dormant_epochs);
        });
        ser.add([&](auto) {
            return zpp::serialize(_proposals);
        });
        ser.add([&](auto) {
            return zpp::serialize(_donations);
        });
        ser.add([&](auto) {
            return zpp::serialize(_conway_start_epoch);
        });
        ser.add([&](auto) {
            return zpp::serialize(_ratify_ready);
        });
        babbage::state::to_zpp(ser);
    }

    void state::from_zpp(parallel_decoder &dec)
    {
        dec.add([&](const auto b) {
            zpp::deserialize(_enact_state, b);
        });
        dec.add([&](const auto b) {
            zpp::deserialize(_ratify_state, b);
        });
        _pulsing_data.from_zpp(dec);
        dec.add([&](const auto b) {
            zpp::deserialize(_committee_hot_keys, b);
        });
        dec.add([&](const auto b) {
            zpp::deserialize(_drep_state, b);
        });
        dec.add([&](const auto b) {
            zpp::deserialize(_num_dormant_epochs, b);
        });
        dec.add([&](const auto b) {
            zpp::deserialize(_proposals, b);
        });
        dec.add([&](const auto b) {
            zpp::deserialize(_donations, b);
        });
        dec.add([&](const auto b) {
            zpp::deserialize(_conway_start_epoch, b);
        });
        dec.add([&](const auto b) {
            zpp::deserialize(_ratify_ready, b);
        });
        babbage::state::from_zpp(dec);
    }

    state::voting_threshold_t state::_committee_voting_threshold(const enact_state_t &st, const gov_action_t &ga) const
    {
        voting_threshold_t threshold { voting_threshold_t::no_voting_threshold_t {} };
        if (st.committee) {
            if (st.params.protocol_ver.bootstrap_phase() || st.committee->active_size(_committee_hot_keys) >= st.params.committee_min_size)
                threshold.val = st.committee->threshold;
            return std::visit<voting_threshold_t>([&](const auto &av) {
                using T = std::decay_t<decltype(av)>;
                if constexpr (std::is_same_v<T, gov_action_t::no_confidence_t>)
                    return voting_threshold_t { voting_threshold_t::no_voting_allowed_t {} };
                if constexpr (std::is_same_v<T, gov_action_t::update_committee_t>)
                    return voting_threshold_t { voting_threshold_t::no_voting_allowed_t {} };
                if constexpr (std::is_same_v<T, gov_action_t::new_constitution_t>)
                    return voting_threshold_t { voting_threshold_t::no_voting_allowed_t {} };
                if constexpr (std::is_same_v<T, gov_action_t::hard_fork_init_t>)
                    return threshold;
                if constexpr (std::is_same_v<T, gov_action_t::parameter_change_t>)
                    return threshold;
                if constexpr (std::is_same_v<T, gov_action_t::treasury_withdrawals_t>)
                    return threshold;
                if constexpr (std::is_same_v<T, gov_action_t::info_action_t>)
                    return voting_threshold_t { voting_threshold_t::no_voting_threshold_t {} };
                throw error(fmt::format("unsupported gov action type: {}", typeid(T).name()));
                return voting_threshold_t { voting_threshold_t::no_voting_allowed_t {} };
            }, ga.val);
        }
        return threshold;
    }

    bool state::committee_accepted(const gov_action_state_t &ga) const
    {
        const auto &st = _ratify_state.new_state;
        size_t yes = 0;
        size_t total = 0;
        if (st.committee) {
            // starting with the list of committee key allows to discard votes
            // of those members who first votes but then resigned
            for (const auto &[cold_id, expire_epoch]: st.committee->members) {
                const auto hot_id_it = _committee_hot_keys.find(cold_id);
                if (hot_id_it != _committee_hot_keys.end() && std::holds_alternative<credential_t>(hot_id_it->second.val)) {
                    const auto &hot_id = std::get<credential_t>(hot_id_it->second.val);
                    const auto v_it = ga.committee_votes.find(hot_id);
                    if (v_it != ga.committee_votes.end()) {
                        if (v_it->second.vote != vote_t::abstain) {
                            ++total;
                            if (v_it->second.vote == vote_t::yes)
                                ++yes;
                        }
                    } else {
                        // No vote is counted as a "no"
                        ++total;
                    }
                }
            }
        }
        const rational_u64 r { yes, std::max(total, size_t { 1 }) };
        return _check_threshold(_committee_voting_threshold(_ratify_state.new_state, ga.proposal.action), r);
    }

    state::default_vote_t state::_pool_default_vote(const pool_hash &id) const
    {
        // default votes are prepared using the pulsing snapshot at the start of the next epoch.
        // therefore, the correct pool_params are in mark set now
        const auto &params = map_nice_at(_mark.pool_params, id);
        const auto acc_it = _accounts.find(params.params.reward_id);
        if (acc_it != _accounts.end() && acc_it->second.vote_deleg) {
            if (std::holds_alternative<drep_t::abstain_t>(acc_it->second.vote_deleg->val))
                return default_vote_t::abstain;
            if (std::holds_alternative<drep_t::no_confidence_t>(acc_it->second.vote_deleg->val))
                return default_vote_t::no_confidence;
        }
        return default_vote_t::no;
    }

    state::voting_threshold_t state::_pool_voting_threshold(const enact_state_t &st, const gov_action_t &ga) const
    {
        const auto &params = st.params;
        const auto has_committee = st.committee.has_value();
        const auto vt = params.pool_voting_thresholds;
        return std::visit<voting_threshold_t>([&](const auto &av) {
            using T = std::decay_t<decltype(av)>;
            if constexpr (std::is_same_v<T, gov_action_t::no_confidence_t>)
                return voting_threshold_t { vt.motion_of_no_confidence };
            if constexpr (std::is_same_v<T, gov_action_t::update_committee_t>) {
                if (has_committee)
                    return voting_threshold_t { vt.committee_normal };
                return voting_threshold_t { vt.committee_no_confidence };
            }
            if constexpr (std::is_same_v<T, gov_action_t::new_constitution_t>)
                return voting_threshold_t { voting_threshold_t::no_voting_allowed_t {} };
            if constexpr (std::is_same_v<T, gov_action_t::hard_fork_init_t>)
                return voting_threshold_t { vt.hard_fork_initiation };
            if constexpr (std::is_same_v<T, gov_action_t::parameter_change_t>) {
                if (av.update.security_group())
                    return voting_threshold_t { vt.security_voting_threshold };
                return voting_threshold_t { voting_threshold_t::no_voting_allowed_t {} };
            }
            if constexpr (std::is_same_v<T, gov_action_t::treasury_withdrawals_t>)
                return voting_threshold_t { voting_threshold_t::no_voting_allowed_t {} };
            if constexpr (std::is_same_v<T, gov_action_t::info_action_t>)
                return voting_threshold_t { voting_threshold_t::no_voting_threshold_t {} };
            throw error(fmt::format("unsupported gov action type: {}", typeid(T).name()));
            return voting_threshold_t { voting_threshold_t::no_voting_allowed_t {} };
        }, ga.val);
    }

    bool state::_check_threshold(const voting_threshold_t &t, const rational_u64 &r)
    {
        return std::visit<bool>([&](const auto &tv) {
            using T = std::decay_t<decltype(tv)>;
            if constexpr (std::is_same_v<T, rational_u64>)
                return r >= tv;
            if constexpr (std::is_same_v<T, voting_threshold_t::no_voting_threshold_t>)
                return false;
            // means that the vote should not count!!!
            if constexpr (std::is_same_v<T, voting_threshold_t::no_voting_allowed_t>)
                return true;
            throw error(fmt::format("unsupported voting_threshold type: {}", typeid(T).name()));
            return false;
        }, t.val);
    }

    bool state::pools_accepted(const gov_action_state_t &ga) const
    {
        uint64_t yes = 0;
        uint64_t abstain = 0;
        // starting with the list of pools allows to handle the cases of no vote differently
        for (const auto &[pool_id, stake]: _pulsing_data.pool_voting_power) {
            const auto v_it = ga.pool_votes.find(pool_id);
            if (v_it != ga.pool_votes.end()) {
                switch (v_it->second.vote) {
                    case vote_t::abstain: abstain += stake; break;
                    case vote_t::no: break;
                    case vote_t::yes: yes += stake; break;
                    default: throw error(fmt::format("unsupported vote value: {}", static_cast<int>(v_it->second.vote)));
                }
            } else {
                if (std::holds_alternative<gov_action_t::hard_fork_init_t>(ga.proposal.action.val)) {
                    // match first and ignore
                } else if (_params.protocol_ver.bootstrap_phase()) {
                    abstain += stake;
                } else {
                    switch (const auto default_vote = _pool_default_vote(pool_id); default_vote) {
                        case default_vote_t::no_confidence:
                            if (std::holds_alternative<gov_action_t::no_confidence_t>(ga.proposal.action.val))
                                yes += stake;
                            break;
                        case default_vote_t::abstain:
                            abstain += stake;
                            break;
                        default:
                            break;
                    }
                }
            }
        }
        const rational_u64 r { yes, std::max(_pulsing_data.pool_voting_power.total_stake() - abstain, uint64_t { 1 }) };
        return _check_threshold(_pool_voting_threshold(_ratify_state.new_state, ga.proposal.action), r);
    }

    rational_u64 state::_param_update_threshold(const param_update_t &upd, const drep_voting_thresholds_t &t) const
    {
        rational_u64 r { 0, 1 };
        if (upd.network_group() && r < t.pp_network_group)
            r = t.pp_network_group;
        if (upd.governance_group() && r < t.pp_governance_group)
            r = t.pp_governance_group;
        if (upd.technical_group() && r < t.pp_technical_group)
            r = t.pp_technical_group;
        if (upd.economic_group() && r < t.pp_economic_group)
            r = t.pp_economic_group;
        return r;
    }

    state::voting_threshold_t state::_drep_voting_threshold(const enact_state_t &st, const gov_action_t &ga) const
    {
        const auto &params = st.params;
        const auto has_committee = st.committee.has_value();
        const auto &t = params.protocol_ver.bootstrap_phase()
            ? drep_voting_thresholds_t::zero()
            : params.drep_voting_thresholds;
        return std::visit<voting_threshold_t>([&](const auto &a) {
            using T = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<T, gov_action_t::no_confidence_t>)
                return voting_threshold_t { t.motion_no_confidence };
            if constexpr (std::is_same_v<T, gov_action_t::update_committee_t>) {
                if (has_committee)
                    return voting_threshold_t { t.committee_normal };
                return voting_threshold_t {  t.committee_no_confidence };
            }
            if constexpr (std::is_same_v<T, gov_action_t::new_constitution_t>)
                return voting_threshold_t { t.update_constitution };
            if constexpr (std::is_same_v<T, gov_action_t::hard_fork_init_t>)
                return voting_threshold_t { t.hard_fork_initiation };
            if constexpr (std::is_same_v<T, gov_action_t::parameter_change_t>)
                return voting_threshold_t { _param_update_threshold(a.update, t) };
            if constexpr (std::is_same_v<T, gov_action_t::treasury_withdrawals_t>)
                return voting_threshold_t {  t.treasury_withdrawal };
            if constexpr (std::is_same_v<T, gov_action_t::info_action_t>)
                return voting_threshold_t { voting_threshold_t::no_voting_threshold_t {} };
            throw error(fmt::format("unsupported governance action type: {}", typeid(T).name()));
            return voting_threshold_t { voting_threshold_t::no_voting_allowed_t {} };
        }, ga.val);
    }

    bool state::dreps_accepted(const gov_action_state_t &ga) const
    {
        uint64_t yes = 0;
        uint64_t total_wo_abstain = 0;
        for (const auto &[drep, stake]: _pulsing_data.drep_voting_power) {
            std::visit([&](const auto &cred) {
                using T = std::decay_t<decltype(cred)>;
                if constexpr (std::is_same_v<T, drep_t::abstain_t>) {
                    // do nothing
                } else if constexpr (std::is_same_v<T, drep_t::no_confidence_t>) {
                    total_wo_abstain += stake;
                    if (std::holds_alternative<gov_action_t::no_confidence_t>(ga.proposal.action.val))
                        yes += stake;
                } else if constexpr (std::is_same_v<T, credential_t>) {
                    const auto d_it = _drep_state.find(cred);
                    if (d_it != _drep_state.end() && _epoch <= d_it->second.expire_epoch) {
                        const auto v_it = ga.drep_votes.find(cred);
                        if (v_it != ga.drep_votes.end()) {
                            switch (v_it->second.vote) {
                                case vote_t::no:
                                    total_wo_abstain += stake;
                                    break;
                                case vote_t::yes:
                                    yes += stake;
                                    total_wo_abstain += stake;
                                    break;
                                case vote_t::abstain:
                                    break;
                                default:
                                    throw error(fmt::format("unsupported vote value: {}", static_cast<int>(v_it->second.vote)));
                            }
                        } else {
                            total_wo_abstain += stake;
                        }
                    }
                } else {
                    throw error(fmt::format("unsupported drep type: {}", typeid(T).name()));
                }
            }, drep.val);
        }
        const rational_u64 r { yes, std::max(total_wo_abstain, uint64_t { 1 }) };
        return _check_threshold(_drep_voting_threshold(_ratify_state.new_state, ga.proposal.action), r);
    }

    bool state::accepted_by_everyone(const gov_action_id_t &gid, const gov_action_state_t &gas) const
    {
        const auto committee_ok = committee_accepted(gas);
        const auto pools_ok = pools_accepted(gas);
        const auto dreps_ok = dreps_accepted(gas);;
        const auto res = committee_ok & pools_ok & dreps_ok;
        logger::debug("epoch: {} voting on {} committee: {} pools: {} dreps: {} => res: {}",
            _epoch, gid, committee_ok, pools_ok, dreps_ok, res);
        return res;
    }

    // Why it's checked here not at the time of proposal parsing?
    static bool _valid_committee_term(const gov_action_t &ga, const protocol_params &pp, const uint64_t epoch)
    {
        if (std::holds_alternative<gov_action_t::update_committee_t>(ga.val)) {
            const auto max_expire_epoch = epoch + pp.committee_max_term_length;
            const auto &new_c = std::get<gov_action_t::update_committee_t>(ga.val);
            for (const auto &[cred, expire_epoch]: new_c.members_to_add) {
                if (expire_epoch > max_expire_epoch)
                    return false;
            }
            return true;
        }
        return true;
    }

    static bool _withdrawals_can_withdraw(const gov_action_t &ga, const uint64_t treasury)
    {
        if (std::holds_alternative<gov_action_t::treasury_withdrawals_t>(ga.val)) {
            uint64_t withdrawals = 0;
            const auto &w = std::get<gov_action_t::treasury_withdrawals_t>(ga.val);
            for (const auto &[cred, coin]: w.withdrawals)
                withdrawals += coin;
            return withdrawals <= treasury;
        }
        return true;
    }

    static bool _prev_action_as_expected(const gov_action_t &ga, const enact_state_t &st)
    {
        return std::visit<bool>([&](const auto &a) {
            using T = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<T, gov_action_t::parameter_change_t>)
                return !a.prev_action_id || (!st.prev_actions.param_updates.empty() && *a.prev_action_id == st.prev_actions.param_updates.back());
            if constexpr (std::is_same_v<T, gov_action_t::hard_fork_init_t>)
                return !a.prev_action_id || (!st.prev_actions.hard_forks.empty() && *a.prev_action_id == st.prev_actions.hard_forks.back());
            if constexpr (std::is_same_v<T, gov_action_t::update_committee_t>)
                return !a.prev_action_id || (!st.prev_actions.committee_updates.empty() && *a.prev_action_id == st.prev_actions.committee_updates.back());
            if constexpr (std::is_same_v<T, gov_action_t::new_constitution_t>)
                return !a.prev_action_id || (!st.prev_actions.constitution_updates.empty() && *a.prev_action_id == st.prev_actions.constitution_updates.back());
            return true;
        }, ga.val);
    }

    void state::_enact_proposal(enact_state_t &st, const gov_action_id_t &gid, const gov_action_t &ga)
    {
        std::visit([&](const auto &a) {
            using T = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<T, gov_action_t::parameter_change_t>) {
                logger::info("enacting new protocol parameters: {}", a.update);
                st.params.apply(a.update);
                st.prev_actions.param_updates.emplace_back(gid);
            } else if constexpr (std::is_same_v<T, gov_action_t::hard_fork_init_t>) {
                logger::info("enacting new protocol version: {}", a.protocol_ver);
                st.params.protocol_ver = a.protocol_ver;
                st.prev_actions.hard_forks.emplace_back(gid);
            } else if constexpr (std::is_same_v<T, gov_action_t::treasury_withdrawals_t>) {
                for (const auto &[reward_id, coin]: a.withdrawals) {
                    st.withdrawals[address { reward_id }.stake_id()] += coin;
                    st.treasury += coin;
                }
            } else if constexpr (std::is_same_v<T, gov_action_t::no_confidence_t>) {
                st.committee.reset();
                st.prev_actions.committee_updates.emplace_back(gid);
            } else if constexpr (std::is_same_v<T, gov_action_t::update_committee_t>) {
                if (!st.committee)
                    st.committee.emplace();
                for (const auto &cred: a.members_to_remove)
                    st.committee->members.erase(cred);
                for (const auto &[cred, expire_epoch]: a.members_to_add)
                    st.committee->members[cred] = expire_epoch;
                st.committee->threshold = a.new_threshold;
                st.prev_actions.committee_updates.emplace_back(gid);
            } else if constexpr (std::is_same_v<T, gov_action_t::new_constitution_t>) {
                st.constitution = a.new_constitution;
                st.prev_actions.constitution_updates.emplace_back(gid);
            } else if constexpr (std::is_same_v<T, gov_action_t::info_action_t>) {
                // do nothing
            } else {
                throw error(fmt::format("unsupported gov action type: {}", typeid(T).name()));
            }
        }, ga.val);
    }

    void state::_transfer_treasury_withdrawals(const stake_distribution &rewards)
    {
        for (const auto &[stake_id, reward]: rewards) {
            if (auto acc_it = _accounts.find(stake_id); acc_it != _accounts.end() && acc_it->second.ptr) {
                _treasury -= reward;
                acc_it->second.reward += reward;
                if (acc_it->second.deleg)
                    _active_pool_dist.add(*acc_it->second.deleg, reward);
            } else {
                logger::warn("treasury withdrawal ignored - no such reward account: {}", stake_id);
            }
        }
    }

    drep_distr_t state::_compute_drep_voting_power() const
    {
        drep_distr_t power {};
        if (!_drep_state.empty()) {
            static const std::string task_id { "drep-voting-power" };
            mutex::unique_lock::mutex_type drep_mutex alignas(mutex::alignment) {};
            _sched.wait_all(task_id, [&](const auto &todo, const auto &submit_f) {
                for (size_t part_no = 0; part_no < _accounts.num_parts; ++part_no) {
                    submit_f({ 1000, task_id, [&, part_no, todo] {
                        drep_distr_t part_stake {};
                        const auto &acc_part = _accounts.partition(part_no);
                        for (const auto &[stake_id, info]: acc_part) {
                            if (info.vote_deleg && (!std::holds_alternative<credential_t>(info.vote_deleg->val) || _drep_state.contains(std::get<credential_t>(info.vote_deleg->val)))) {
                                part_stake[*info.vote_deleg] += info.mark_stake;
                            }
                        }
                        mutex::scoped_lock lk { drep_mutex };
                        for (const auto &[drep, stake]: part_stake) {
                            power[drep] += stake;
                        }
                    }});
                }
            });
        }
        return power;
    }

    pool_stake_distribution state::_compute_pool_voting_power() const
    {
        pool_stake_distribution power {};
        for (const auto &[pool_id, stake]: _mark.pool_dist) {
            if (const auto num_delegs = map_nice_at(_mark.inv_delegs, pool_id).size(); num_delegs) {
                power.create(pool_id);
                power.add(pool_id, stake);
            }
        }
        for (const auto &[gid, ga]: _proposals) {
            const auto acc_it = _accounts.find(ga.proposal.return_addr);
            if (acc_it != _accounts.end() && acc_it->second.deleg)
                power.add(*acc_it->second.deleg, ga.proposal.deposit);
        }
        return power;
    }

    const pulsing_data_t &state::pulser_data() const
    {
        return _pulsing_data;
    }

    void state::_gov_remove_proposal(const gov_action_id_t &gid) {
        const auto &gas = map_nice_at(_proposals, gid);
        if (auto acc_it = _accounts.find(gas.proposal.return_addr); acc_it != _accounts.end() && acc_it->second.ptr) {
            logger::debug("epoch: {} returning proposal {} deposit to {} registered: {}",
                _epoch, gid, gas.proposal.return_addr, acc_it->second.ptr);
            acc_it->second.reward += gas.proposal.deposit;
            if (acc_it->second.deleg)
                _active_pool_dist.add(*acc_it->second.deleg, gas.proposal.deposit);
        } else {
            logger::debug("epoch: {} proposal {} cannot return the deposit to an unregistered stake_id: {}",
                _epoch, gid, gas.proposal.return_addr);
            _treasury += gas.proposal.deposit;
        }
        _deposited -= gas.proposal.deposit;
        _proposals.erase(gid);
    }

    void state::_gov_enact()
    {
        for (const auto &gid: _ratify_state.expired)
            _gov_remove_proposal(gid);
        for (const auto &[gid, gas]: _ratify_state.enacted)
            _gov_remove_proposal(gid);

        // copy the ratified state and prepare the ratification state for the next round
        _params_prev = _params;
        _enact_state = _ratify_state.new_state;
        _params = _enact_state.params;
        _transfer_treasury_withdrawals(_enact_state.withdrawals);
        _ratify_state.enacted.clear();
        _ratify_state.expired.clear();
        _ratify_state.delayed = false;
        _ratify_state.new_state.prev_params = _params_prev;
        _ratify_state.new_state.withdrawals.clear();

        _treasury += _donations;
        _donations = 0;
    }

    void state::_gov_finalize()
    {
        std::vector<gov_action_id_t> retired_actions {};
        for (const auto &[gid, gas]: _pulsing_data.proposals) {
            if (!std::holds_alternative<gov_action_t::info_action_t>(gas.proposal.action.val)
                    && _prev_action_as_expected(gas.proposal.action, _ratify_state.new_state)
                    && _valid_committee_term(gas.proposal.action, _ratify_state.new_state.params, _epoch)
                    && !_ratify_state.delayed
                    && _withdrawals_can_withdraw(gas.proposal.action, _treasury)
                    && accepted_by_everyone(gid, gas)) {
                _enact_proposal(_ratify_state.new_state, gid, gas.proposal.action);
                _ratify_state.enacted.emplace_back(gid, gas);
                _ratify_state.delayed = gas.proposal.action.delaying();
            } else if (_epoch > gas.expires_after) {
                _ratify_state.expired.emplace(gid);
            }
        }
        _ratify_ready = true;
    }

    void state::_gov_make_pulsing_snapshot()
    {
        _pulsing_data.drep_state_updated = false;
        _pulsing_data.drep_state = _drep_state;
        {
            _pulsing_data.proposals.clear();
            _pulsing_data.proposals.reserve(_proposals.size());
            for (const auto &[gid, gas]: _proposals) {
                _pulsing_data.proposals.emplace_back(gid, gas);
            }
            std::sort(
                _pulsing_data.proposals.begin(), _pulsing_data.proposals.end(),
                [](const auto &l, const auto &r) {
                    if (auto cmp = l.second.proposal.action.priority() - r.second.proposal.action.priority(); cmp != 0)
                        return cmp < 0;
                    return l.second.loc < r.second.loc;
                }
            );
        }
        _pulsing_data.pool_voting_power = _compute_pool_voting_power();
        _pulsing_data.drep_voting_power = _compute_drep_voting_power();
    }

    // Called for every Conway epoch but the first one!
    void state::start_epoch(const std::optional<uint64_t> new_epoch)
    {
        babbage::state::start_epoch(new_epoch);
        _gov_enact();

        if (!_conway_start_epoch)
            _conway_start_epoch.emplace(_epoch);
        if (!_stake_pointers.empty() && _epoch > *_conway_start_epoch)
            _stake_pointers.clear();

        if (_params.protocol_ver.major >= 10 && _params_prev.protocol_ver.major < 10) {
            // Recreate drep delegation state as Cardano Ledger PV 9 had a bug
            for (auto &[drep, state]: _drep_state)
                state.delegs.clear();
            for (const auto &[id, info]: _accounts) {
                if (info.vote_deleg && std::holds_alternative<credential_t>(info.vote_deleg->val)) {
                    // Not all dreps may be still present! That's the consequence of the bug!
                    const auto &drep_id = std::get<credential_t>(info.vote_deleg->val);
                    if (const auto it = _drep_state.find(drep_id); it != _drep_state.end()) {
                        it->second.delegs.emplace(id);
                    }
                }
            }
        }

        if (_proposals.empty()) {
            ++_num_dormant_epochs;
        } else if (_num_dormant_epochs) {
            for (auto &[id, state]: _drep_state)
                state.expire_epoch += _num_dormant_epochs;
            _num_dormant_epochs = 0;
        }

        _gov_make_pulsing_snapshot();
        _ratify_ready = false;
    }

    void state::run_pulser_if_ready()
    {
        babbage::state::run_pulser_if_ready();
        if (_epoch_slot >= _cfg.shelley_rewards_ready_slot && !_ratify_ready)
            _gov_finalize();
    }

    bool state::has_gov_action(const gov_action_id_t &gid) const
    {
        return _proposals.contains(gid);
    }

    const gov_action_state_t &state::gov_action(const gov_action_id_t &gid) const
    {
        return map_nice_at(_proposals, gid);
    }

    const optional_committee_t &state::committee() const
    {
        return _enact_state.committee;
    }
}
