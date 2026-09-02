/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <algorithm>
#include <turbo/cardano/ledger/conway.hpp>

namespace turbo::cardano::ledger::conway {
    void committee_t::hot_key_t::to_cbor(era_encoder &enc) const
    {
        std::visit([&](auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, credential_t>) {
                enc.array(2);
                enc.uint(0);
                v.to_cbor(enc);
            } else if constexpr (std::is_same_v<T, resigned_t>) {
                enc.array(2);
                enc.uint(1);
                v.anchor.to_cbor(enc);
            } else {
                throw error(fmt::format("unsupported hot_key_t value: {}", typeid(T).name()));
            }
        }, val);
    }

    void drep_info_t::to_cbor(era_encoder &enc) const
    {
        enc.array(4);
        enc.uint(expire_epoch);
        // Ledger-state format is not compatible with the block format.
        if (anchor) {
            enc.array(1);
            anchor->to_cbor(enc);
        } else {
            enc.array(0);
        }
        enc.uint(deposited);
        enc.tag(258);
        enc.array_compact(delegs.size(), [&] {
            const auto encode_delegs = [&](const bool script) {
                for (const auto &deleg: delegs) {
                    if (deleg.script == script)
                        deleg.to_cbor(enc);
                }
            };
            encode_delegs(true);
            encode_delegs(false);
        });
    }

    void committee_t::to_cbor(era_encoder &enc) const
    {
        enc.array(2);
        enc.map_compact(members.size(), [&] {
            const auto encode_members = [&](const bool script) {
                for (const auto &[cred, epoch]: members) {
                    if (cred.script == script) {
                        cred.to_cbor(enc);
                        enc.uint(epoch);
                    }
                }
            };
            encode_members(true);
            encode_members(false);
        });
        threshold.to_cbor(enc);
    }

    void gov_action_state_t::to_cbor(era_encoder &enc, const gov_action_id_t &id) const
    {
        enc.array(7);
        id.to_cbor(enc);
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
        enc.map_compact(pool_votes.size(), [&] {
            for (const auto &[id, vote]: pool_votes) {
                enc.bytes(id);
                vote.to_cbor(enc);
            }
        });
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
        constitution_updates.to_cbor(enc);
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
            for (const auto &[id, gas]: enacted)
                gas.to_cbor(enc, id);
        });
        expired.to_cbor(enc);
        if (delayed)
            enc.s_true();
        else
            enc.s_false();
    }

    void state::_add_encode_task(cbor_encoder &ser, const encode_cbor_func &t) const
    {
        ser.add([t](auto enc) {
            t(enc);
            return std::move(enc.cbor());
        });
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
                for (const auto &[id, action]: proposals_copy)
                    action.to_cbor(enc, id);
            });
        }
        _enact_state.committee.to_cbor(enc);
        _enact_state.constitution.to_cbor(enc);
        _params_to_cbor(enc, _params);
        _params_to_cbor(enc, _params_prev);
        if (_ratify_state.new_state.params != _params) {
            enc.array(2);
            enc.uint(1);
            _params_to_cbor(enc, _ratify_state.new_state.params);
        } else {
            enc.array(1).uint(0);
        }
        {
            enc.array(2);
            {
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
                enc.map_compact(_pulsing_data.pool_voting_power.size(), [&] {
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
        if (acc.deleg)
            enc.array(1).bytes(*acc.deleg);
        else
            enc.array(0);
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
            const auto encode_hot_keys = [&](const bool script) {
                for (const auto &[cold_id, hot_id]: _committee_hot_keys) {
                    if (cold_id.script == script) {
                        cold_id.to_cbor(enc);
                        hot_id.to_cbor(enc);
                    }
                }
            };
            encode_hot_keys(true);
            encode_hot_keys(false);
        });
        enc.uint(_num_dormant_epochs);
    }
}

