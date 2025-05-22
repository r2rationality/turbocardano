/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/cardano/common/cert.hpp>

namespace daedalus_turbo::cardano {
    stake_reg_cert stake_reg_cert::from_cbor(cbor::zero2::array_reader &it)
    {
        return { credential_t::from_cbor(it.read()) };
    }

    stake_dereg_cert stake_dereg_cert::from_cbor(cbor::zero2::array_reader &it)
    {
        return { credential_t::from_cbor(it.read()) };
    }

    stake_deleg_cert stake_deleg_cert::from_cbor(cbor::zero2::array_reader &it)
    {
        return { credential_t::from_cbor(it.read()), it.read().bytes() };
    }

    pool_reg_cert pool_reg_cert::from_cbor(cbor::zero2::array_reader &it)
    {
        return { it.read().bytes(), pool_params::from_cbor(it) };
    }

    pool_retire_cert pool_retire_cert::from_cbor(cbor::zero2::array_reader &it)
    {
        return { it.read().bytes(), it.read().uint() };
    }

    genesis_deleg_cert genesis_deleg_cert::from_cbor(cbor::zero2::array_reader &it)
    {
        return { it.read().bytes(), it.read().bytes(), it.read().bytes() };
    }

    reward_source reward_source_from_cbor(cbor::zero2::value &v)
    {
        switch (const auto source_raw = v.uint(); source_raw) {
            case 0: return reward_source::reserves;
            case 1: return reward_source::treasury;
            default: throw error(fmt::format("unexpected value of reward source: {}!", source_raw));
        }
    }

    instant_reward_cert instant_reward_cert::from_cbor(cbor::zero2::array_reader &it)
    {
        auto &reward = it.read();
        auto &r_it = reward.array();
        return { reward_source_from_cbor(r_it.read()), decltype(rewards)::from_cbor(r_it.read()) };
    }

    resign_committee_cold_cert resign_committee_cold_cert::from_cbor(cbor::zero2::array_reader &it)
    {
        return {
            decltype(cold_id)::from_cbor(it.read()),
            decltype(anchor)::from_cbor(it.read())
        };
    }

    reg_drep_cert reg_drep_cert::from_cbor(cbor::zero2::array_reader &it)
    {
        return {
            decltype(drep_id)::from_cbor(it.read()),
            it.read().uint(),
            decltype(anchor)::from_cbor(it.read())
        };
    }

    update_drep_cert update_drep_cert::from_cbor(cbor::zero2::array_reader &it)
    {
        return  {
            decltype(drep_id)::from_cbor(it.read()),
            decltype(anchor)::from_cbor(it.read())
        };
    }

    cert_t cert_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        switch (const auto typ = it.read().uint(); typ) {
            case 0: return { stake_reg_cert::from_cbor(it) };
            case 1: return { stake_dereg_cert::from_cbor(it) };
            case 2: return { stake_deleg_cert::from_cbor(it) };
            case 3: return { pool_reg_cert::from_cbor(it) };
            case 4: return { pool_retire_cert::from_cbor(it) };
            case 5: return { genesis_deleg_cert::from_cbor(it) };
            case 6: return { instant_reward_cert::from_cbor(it) };
            case 7: return { reg_cert {
                credential_t::from_cbor(it.read()),
                it.read().uint()
            } };
            case 8: return { unreg_cert {
                credential_t::from_cbor(it.read()),
                it.read().uint()
            } };
            case 9: return { vote_deleg_cert {
                credential_t::from_cbor(it.read()),
                drep_t::from_cbor(it.read())
            } };
            case 10: return { stake_vote_deleg_cert {
                credential_t::from_cbor(it.read()),
                it.read().bytes(),
                drep_t::from_cbor(it.read())
            } };
            case 11: return { stake_reg_deleg_cert {
                credential_t::from_cbor(it.read()),
                it.read().bytes(),
                it.read().uint()
            } };
            case 12: return { vote_reg_deleg_cert {
                credential_t::from_cbor(it.read()),
                drep_t::from_cbor(it.read()),
                it.read().uint()
            } };
            case 13: return { stake_vote_reg_deleg_cert {
                credential_t::from_cbor(it.read()),
                it.read().bytes(),
                drep_t::from_cbor(it.read()),
                it.read().uint()
            } };
            case 14: return { auth_committee_hot_cert {
                credential_t::from_cbor(it.read()),
                credential_t::from_cbor(it.read())
            } };
            case 15: return { resign_committee_cold_cert::from_cbor(it) };
            case 16: return { reg_drep_cert::from_cbor(it) };
            case 17: return { unreg_drep_cert {
                credential_t::from_cbor(it.read()),
                it.read().uint()
            } };
            case 18: return { update_drep_cert::from_cbor(it) };
            [[unlikely]] default:
                throw error(fmt::format("unsupported cert type: {}", typ));
        }
    }

    std::optional<credential_t> cert_t::signing_cred() const
    {
        std::optional<credential_t> cred {};
        std::visit([&](const auto &c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, auth_committee_hot_cert>
                    || std::is_same_v<T, resign_committee_cold_cert>) {
                cred.emplace(c.cold_id);
            } else if constexpr (std::is_same_v<T, reg_drep_cert>
                   || std::is_same_v<T, unreg_drep_cert>
                   || std::is_same_v<T, update_drep_cert>) {
                cred.emplace(c.drep_id);
            } else if constexpr (std::is_same_v<T, pool_reg_cert>
                    || std::is_same_v<T, pool_retire_cert>) {
                cred.emplace(c.pool_id, false);
            } else if constexpr (std::is_same_v<T, genesis_deleg_cert>) {
                cred.emplace(c.hash, false);
            } else if constexpr (std::is_same_v<T, stake_reg_cert>) {
                // nothing - stake registration does not require certification
            } else if constexpr (std::is_same_v<T, instant_reward_cert>) {
                // nothing here - a quorum of genesis signers is checked in a different way
            } else {
                cred.emplace(c.stake_id);
            }
        }, val);
        return cred;
    }

    anchor_t anchor_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return { std::string { it.read().text() }, it.read().bytes() };
    }

    anchor_t anchor_t::from_json(const json::value &j)
    {
        return { json::value_to<std::string>(j.at("url")), datum_hash::from_hex(j.at("dataHash").as_string()) };
    }

    void anchor_t::to_cbor(era_encoder &enc) const
    {
        enc.array(2).text(url).bytes(hash);
    }

    param_update_t param_update_t::from_cbor(cbor::zero2::value &v)
    {
        param_update_t upd {};
        auto &it = v.map();
        while (!it.done()) {
            auto &key = it.read_key();
            const auto typ = key.uint();
            auto &u = it.read_val(std::move(key));
            switch (typ) {
                case 0: upd.min_fee_a.emplace(u.uint()); break;
                case 1: upd.min_fee_b.emplace(u.uint()); break;
                case 2: upd.max_block_body_size.emplace(numeric_cast<uint32_t>(u.uint())); break;
                case 3: upd.max_transaction_size.emplace(numeric_cast<uint32_t>(u.uint())); break;
                case 4: upd.max_block_header_size.emplace(numeric_cast<uint16_t>(u.uint())); break;
                case 5: upd.key_deposit.emplace(u.uint()); break;
                case 6: upd.pool_deposit.emplace(u.uint()); break;
                case 7: upd.e_max.emplace(numeric_cast<uint32_t>(u.uint())); break;
                case 8: upd.n_opt.emplace(u.uint()); break;
                case 9: upd.pool_pledge_influence = decltype(upd.pool_pledge_influence)::value_type::from_cbor(u); break;
                case 10: upd.expansion_rate = decltype(upd.expansion_rate)::value_type::from_cbor(u); break;
                case 11: upd.treasury_growth_rate = decltype(upd.treasury_growth_rate)::value_type::from_cbor(u); break;
                case 16: upd.min_pool_cost.emplace(u.uint()); break;
                case 17: upd.lovelace_per_utxo_byte.emplace(u.uint()); break;
                case 18: upd.plutus_cost_models = decltype(upd.plutus_cost_models)::value_type::from_cbor(u); break;
                case 19: upd.ex_unit_prices = decltype(upd.ex_unit_prices)::value_type::from_cbor(u); break;
                case 20: upd.max_tx_ex_units = decltype(upd.max_tx_ex_units)::value_type::from_cbor(u); break;
                case 21: upd.max_block_ex_units = decltype(upd.max_block_ex_units)::value_type::from_cbor(u); break;
                case 22: upd.max_value_size.emplace(u.uint()); break;
                case 23: upd.max_collateral_pct.emplace(u.uint()); break;
                case 24: upd.max_collateral_inputs.emplace(u.uint()); break;
                case 25: upd.pool_voting_thresholds = decltype(upd.pool_voting_thresholds)::value_type::from_cbor(u); break;
                case 26: upd.drep_voting_thresholds = decltype(upd.drep_voting_thresholds)::value_type::from_cbor(u); break;
                case 27: upd.committee_min_size.emplace(numeric_cast<uint16_t>(u.uint())); break;
                case 28: upd.committee_max_term_length.emplace(numeric_cast<uint32_t>(u.uint())); break;
                case 29: upd.gov_action_lifetime.emplace(numeric_cast<uint32_t>(u.uint())); break;
                case 30: upd.gov_action_deposit.emplace(u.uint()); break;
                case 31: upd.drep_deposit.emplace(u.uint()); break;
                case 32: upd.drep_activity.emplace(numeric_cast<uint32_t>(u.uint())); break;
                case 33: upd.min_fee_ref_script_cost_per_byte = decltype(upd.min_fee_ref_script_cost_per_byte)::value_type::from_cbor(u); break;
                default: throw error(fmt::format("unsupported conway param update: {}", u.to_string()));
            }
        }
        return upd;
    }

    template<std::integral T>
    size_t param_encode(era_encoder &enc, const size_t id, std::optional<T> val)
    {
        if (val) {
            enc.uint(id);
            enc.uint(*val);
            return 1;
        }
        return 0;
    }

    template<typename T>
    size_t param_encode(era_encoder &enc, const size_t id, std::optional<T> val)
    {
        if (val) {
            enc.uint(id);
            val->to_cbor(enc);
            return 1;
        }
        return 0;
    }

    void param_update_t::to_cbor(era_encoder &enc) const
    {
        auto l_enc { enc };
        size_t cnt = 0;
        cnt += param_encode(l_enc, 0, min_fee_a);
        cnt += param_encode(l_enc, 1, min_fee_b);
        cnt += param_encode(l_enc, 2, max_block_body_size);
        cnt += param_encode(l_enc, 3, max_transaction_size);
        cnt += param_encode(l_enc, 4, max_block_header_size);
        cnt += param_encode(l_enc, 5, key_deposit);
        cnt += param_encode(l_enc, 6, pool_deposit);
        cnt += param_encode(l_enc, 7, e_max);
        cnt += param_encode(l_enc, 8, n_opt);
        cnt += param_encode(l_enc, 9, pool_pledge_influence);
        cnt += param_encode(l_enc, 10, expansion_rate);
        cnt += param_encode(l_enc, 11, treasury_growth_rate);
        cnt += param_encode(l_enc, 16, min_pool_cost);
        cnt += param_encode(l_enc, 17, lovelace_per_utxo_byte);
        cnt += param_encode(l_enc, 18, plutus_cost_models);
        cnt += param_encode(l_enc, 19, ex_unit_prices);
        cnt += param_encode(l_enc, 20, max_tx_ex_units);
        cnt += param_encode(l_enc, 21, max_block_ex_units);
        cnt += param_encode(l_enc, 22, max_value_size);
        cnt += param_encode(l_enc, 23, max_collateral_pct);
        cnt += param_encode(l_enc, 24, max_collateral_inputs);
        cnt += param_encode(l_enc, 25, pool_voting_thresholds);
        cnt += param_encode(l_enc, 26, drep_voting_thresholds);
        cnt += param_encode(l_enc, 27, committee_min_size);
        cnt += param_encode(l_enc, 28, committee_max_term_length);
        cnt += param_encode(l_enc, 29, gov_action_lifetime);
        cnt += param_encode(l_enc, 30, gov_action_deposit);
        cnt += param_encode(l_enc, 31, drep_deposit);
        cnt += param_encode(l_enc, 32, drep_activity);
        cnt += param_encode(l_enc, 33, min_fee_ref_script_cost_per_byte);
        enc.map_compact(cnt, [&] {
            enc << l_enc;
        });
    }

    bool param_update_t::security_group() const
    {
        bool res = false;
        res |= min_fee_a.has_value();
        res |= min_fee_b.has_value();
        res |= max_block_body_size.has_value();
        res |= max_transaction_size.has_value();
        res |= max_block_header_size.has_value();
        res |= lovelace_per_utxo_byte.has_value();
        res |= max_block_ex_units.has_value();
        res |= max_value_size.has_value();
        res |= gov_action_deposit.has_value();
        res |= min_fee_ref_script_cost_per_byte.has_value();
        return res;
    }

    bool param_update_t::network_group() const
    {
        bool res = false;
        res |= max_block_body_size.has_value();
        res |= max_transaction_size.has_value();
        res |= max_block_header_size.has_value();
        res |= max_tx_ex_units.has_value();
        res |= max_block_ex_units.has_value();
        res |= max_value_size.has_value();
        res |= max_collateral_inputs.has_value();
        return res;
    }

    bool param_update_t::economic_group() const
    {
        bool res = false;
        res |= min_fee_a.has_value();
        res |= min_fee_b.has_value();
        res |= key_deposit.has_value();
        res |= pool_deposit.has_value();
        res |= lovelace_per_utxo_byte.has_value();
        res |= ex_unit_prices.has_value();
        res |= min_fee_ref_script_cost_per_byte.has_value();
        // res |= min_ref_script_size_per_tx.has_value();
        // res |= min_ref_script_size_per_block.has_value();
        // res |= ref_script_cost_stride.has_value();
        // res |= ref_script_cost_multiplier.has_value();
        return res;
    }

    bool param_update_t::technical_group() const
    {
        bool res = false;
        res |= e_max.has_value();
        res |= n_opt.has_value();
        res |= expansion_rate.has_value();
        res |= max_collateral_pct.has_value();
        res |= plutus_cost_models.has_value();
        return res;
    }

    bool param_update_t::governance_group() const
    {
        bool res = false;
        res |= pool_voting_thresholds.has_value();
        res |= drep_voting_thresholds.has_value();
        res |= committee_min_size.has_value();
        res |= committee_max_term_length.has_value();
        res |= gov_action_lifetime.has_value();
        res |= gov_action_deposit.has_value();
        res |= drep_deposit.has_value();
        res |= drep_activity.has_value();
        return res;
    }

    gov_action_id_t gov_action_id_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return { it.read().bytes(), numeric_cast<uint16_t>(it.read().uint()) };
    }

    void gov_action_id_t::to_cbor(era_encoder &enc) const
    {
        enc.array(2)
            .bytes(tx_id)
            .uint(idx);
    }

    voter_t::type_t voter_type_from_cbor(cbor::zero2::value &v)
    {
        switch (const auto typ = v.uint(); typ) {
            case 0: return voter_t::const_comm_key;
            case 1: return voter_t::const_comm_script;
            case 2: return voter_t::drep_key;
            case 3: return voter_t::drep_script;
            case 4: return voter_t::pool_key;
            default: throw error(fmt::format("unsupported voter type: {}", typ));
        }
    }

    voter_t voter_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return { voter_type_from_cbor(it.read()), it.read().bytes() };
    }

    proposal_procedure_t proposal_procedure_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return {
            it.read().uint(),
            address { it.read().bytes() }.stake_id(),
            gov_action_t::from_cbor(it.read()),
            anchor_t::from_cbor(it.read())
        };
    }

    void proposal_procedure_t::to_cbor(era_encoder &enc) const
    {
        enc.array(4);
        enc.uint(deposit);
        byte_array<sizeof(return_addr.hash) + 1> stake_addr;
        stake_addr[0] = return_addr.script ? 0xF1 : 0xE1;
        memcpy(stake_addr.data() + 1, return_addr.hash.data(), return_addr.hash.size());
        enc.bytes(stake_addr);
        action.to_cbor(enc);
        anchor.to_cbor(enc);
    }

    proposal_t proposal_t::from_cbor(const gov_action_id_t &id_, cbor::zero2::value &v)
    {
        return { id_, proposal_procedure_t::from_cbor(v) };
    }

    vote_t vote_from_cbor(cbor::zero2::value &v)
    {
        switch (const auto vote = v.uint(); vote) {
            case 0: return vote_t::no;
            case 1: return vote_t::yes;
            case 2: return vote_t::abstain;
            default: throw error(fmt::format("unsupported vote: {}", vote));
        }
    }

    voting_procedure_t voting_procedure_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return { vote_from_cbor(it.read()), decltype(anchor)::from_cbor(it.read()) };
    }

    void voting_procedure_t::to_cbor(era_encoder &enc) const
    {
        switch (vote) {
            case vote_t::no: enc.uint(0); break;
            case vote_t::yes: enc.uint(1); break;
            case vote_t::abstain: enc.uint(2); break;
            default: throw error(fmt::format("unsupported vote: {}", static_cast<int>(vote)));
        }
    }

    constitution_t constitution_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return { decltype(anchor)::from_cbor(it.read()), decltype(policy_id)::from_cbor(it.read()) };
    }

    constitution_t constitution_t::from_json(const json::value &j)
    {
        return {
            anchor_t::from_json(j.at("anchor")),
            script_hash::from_hex(j.at("script").as_string())
        };
    }

    void constitution_t::to_cbor(era_encoder &enc) const
    {
        enc.array(2);
        anchor.to_cbor(enc);
        policy_id.to_cbor(enc);
    }

    gov_action_t::parameter_change_t gov_action_t::parameter_change_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return {
            optional_gov_action_id_t::from_cbor(it.read()),
            param_update_t::from_cbor(it.read()),
            optional_script_t::from_cbor(it.read())
        };
    }

    void gov_action_t::parameter_change_t::to_cbor(era_encoder &enc) const
    {
        enc.array(4);
        enc.uint(0);
        prev_action_id.to_cbor(enc);
        update.to_cbor(enc);
        policy_id.to_cbor(enc);
    }

    gov_action_t::hard_fork_init_t gov_action_t::hard_fork_init_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return {
            optional_gov_action_id_t::from_cbor(it.read()),
            protocol_version::from_cbor(it.read())
        };
    }

    void gov_action_t::hard_fork_init_t::to_cbor(era_encoder &enc) const
    {
        enc.array(3);
        enc.uint(1);
        prev_action_id.to_cbor(enc);
        protocol_ver.to_cbor(enc);
    }

    gov_action_t::treasury_withdrawals_t gov_action_t::treasury_withdrawals_t::from_cbor(cbor::zero2::array_reader &it)
    {
        withdrawal_map withdrawals {};
        {
            auto &wdh = it.read();
            auto &w_it = wdh.map();
            while (!w_it.done()) {
                auto &key = w_it.read_key();
                const auto addr = key.bytes();
                auto &coin = w_it.read_val(std::move(key));
                withdrawals.emplace_hint(withdrawals.end(), addr, coin.uint());
            }
        }
        return {
            std::move(withdrawals),
            optional_script_t::from_cbor(it.read())
        };
    }

    void gov_action_t::treasury_withdrawals_t::to_cbor(era_encoder &enc) const
    {
        enc.array(3);
        enc.uint(2);
        enc.map_compact(withdrawals.size(), [&] {
            for (const auto &[reward_id, coin]: withdrawals) {
                enc.bytes(reward_id);
                enc.uint(coin);
            }
        });
        policy_id.to_cbor(enc);
    }

    gov_action_t::no_confidence_t gov_action_t::no_confidence_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return { optional_gov_action_id_t::from_cbor(it.read()) };
    }

    void gov_action_t::no_confidence_t::to_cbor(era_encoder &enc) const
    {
        enc.array(2);
        enc.uint(3);
        prev_action_id.to_cbor(enc);
    }

    gov_action_t::update_committee_t gov_action_t::update_committee_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return {
            decltype(prev_action_id)::from_cbor(it.read()),
            decltype(members_to_remove)::from_cbor(it.read()),
            decltype(members_to_add)::from_cbor(it.read()),
            decltype(new_threshold)::from_cbor(it.read())
        };
    }

    void gov_action_t::update_committee_t::to_cbor(era_encoder &enc) const
    {
        enc.array(5);
        enc.uint(4);
        prev_action_id.to_cbor(enc);
        members_to_remove.to_cbor(enc);
        enc.map_compact(members_to_add.size(), [&] {
            for (const auto &[id, epoch]: members_to_add) {
                id.to_cbor(enc);
                enc.uint(epoch);
            }
        });
        new_threshold.to_cbor(enc);
    }

    gov_action_t::new_constitution_t gov_action_t::new_constitution_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return {
            optional_gov_action_id_t::from_cbor(it.read()),
            constitution_t::from_cbor(it.read())
        };
    }

    void gov_action_t::new_constitution_t::to_cbor(era_encoder &enc) const
    {
        enc.array(3);
        enc.uint(5);
        prev_action_id.to_cbor(enc);
        new_constitution.to_cbor(enc);
    }

    gov_action_t::info_action_t gov_action_t::info_action_t::from_cbor(cbor::zero2::array_reader &)
    {
        return {};
    }

    void gov_action_t::info_action_t::to_cbor(era_encoder &enc) const
    {
        enc.array(1);
        enc.uint(6);
    }

    static gov_action_t::value_type gov_action_t_from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        switch (const auto typ = it.read().uint(); typ) {
            case 0: return gov_action_t::parameter_change_t::from_cbor(it);
            case 1: return gov_action_t::hard_fork_init_t::from_cbor(it);
            case 2: return gov_action_t::treasury_withdrawals_t::from_cbor(it);
            case 3: return gov_action_t::no_confidence_t::from_cbor(it);
            case 4: return gov_action_t::update_committee_t::from_cbor(it);
            case 5: return gov_action_t::new_constitution_t::from_cbor(it);
            case 6: return gov_action_t::info_action_t::from_cbor(it);
            default: throw error(fmt::format("unsupported gov action type: {}", typ));
        }
    }

    gov_action_t gov_action_t::from_cbor(cbor::zero2::value &v)
    {
        return { gov_action_t_from_cbor(v) };
    }

    void gov_action_t::to_cbor(era_encoder &enc) const
    {
        std::visit([&](const auto &v) {
            v.to_cbor(enc);
        }, val);
    }

    bool gov_action_t::delaying() const
    {
        return std::visit<bool>([&](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, no_confidence_t>)
                return true;
            if constexpr (std::is_same_v<T, hard_fork_init_t>)
                return true;
            if constexpr (std::is_same_v<T, update_committee_t>)
                return true;
            if constexpr (std::is_same_v<T, new_constitution_t>)
                return true;
            if constexpr (std::is_same_v<T, treasury_withdrawals_t>)
                return false;
            if constexpr (std::is_same_v<T, parameter_change_t>)
                return false;
            if constexpr (std::is_same_v<T, info_action_t>)
                return false;
            throw error(fmt::format("unsupported gov_action: {}", typeid(T).name()));
            return false;
        }, val);
    }

    int gov_action_t::priority() const
    {
        return std::visit<int>([&](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, no_confidence_t>)
                return 0;
            if constexpr (std::is_same_v<T, update_committee_t>)
                return 1;
            if constexpr (std::is_same_v<T, new_constitution_t>)
                return 2;
            if constexpr (std::is_same_v<T, hard_fork_init_t>)
                return 3;
            if constexpr (std::is_same_v<T, parameter_change_t>)
                return 4;
            if constexpr (std::is_same_v<T, treasury_withdrawals_t>)
                return 5;
            if constexpr (std::is_same_v<T, info_action_t>)
                return 6;
            throw error(fmt::format("unsupported gov_action: {}", typeid(T).name()));
            return std::numeric_limits<int>::max();
        }, val);
    }

    std::strong_ordering gov_action_t::operator<=>(const gov_action_t &o) const
    {
        return priority() <=> o.priority();
    }
}
