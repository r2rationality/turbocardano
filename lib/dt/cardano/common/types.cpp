/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <boost/container/flat_map.hpp>
#include <dt/base64.hpp>
#include <dt/cardano/common/types.hpp>
#include <dt/cardano/common/config.hpp>
#include <dt/cardano/conway/block.hpp>
#include <dt/crypto/crc32.hpp>
#include <dt/crypto/sha3.hpp>
#include <dt/cbor/zero2.hpp>
#include <dt/mutex.hpp>
#include <dt/plutus/costs.hpp>

namespace daedalus_turbo::cardano {
    using namespace crypto;

    point2 point2::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return { it.read().uint(), it.read().bytes() };
    }

    void point2::to_cbor(cbor::encoder &enc) const
    {
        enc.array(2);
        enc.uint(slot);
        enc.bytes(hash);
    }

    point3 point3::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return { point2::from_cbor(it.read()), it.read().uint() };
    }

    void point3::to_cbor(cbor::encoder &enc) const
    {
        enc.array(2);
        point2::to_cbor(enc);
        enc.uint(height);
    }

    void address::to_cbor(era_encoder &enc) const
    {
        if (is_byron() && _bytes[0] == 0x83) {
            enc.bytes(byron_crc_protected(bytes()));
        } else {
            enc.bytes(bytes());
        }
    }

    byron_addr address::byron() const
    {
        if (is_byron()) [[likely]]
            return byron_addr::from_bytes(bytes());
        throw error(fmt::format("address {} is not a byron one!", *this));
    }

    const pay_ident address::pay_id() const
    {
        switch (type()) {
            case 0b0110: // enterprise key
            case 0b0111: // enterprise script
            case 0b0000: // base address: keyhash28,keyhash28
            case 0b0001: // base address: scripthash28,keyhash28
            case 0b0010: // base address: keyhash28,scripthash28
            case 0b0011: // base address: scripthash28,scripthash28
            case 0b0100: // pointer key
            case 0b0101: // pointer script
                return pay_ident { data().subbuf(0, 28), (type() & 0x1) > 0 ? pay_ident::ident_type::SHELLEY_SCRIPT : pay_ident::ident_type::SHELLEY_KEY };

            case 0b1000: { // byron
                auto w_data_pv = cbor::zero2::parse(data());
                auto &w_data = w_data_pv.get();
                if (w_data.indefinite()) [[unlikely]]
                    throw error("indfinite arrays are not supported in byron addresses!");
                switch (const auto n_items = w_data.special_uint(); n_items) {
                    case 3: return pay_ident { w_data.array().read().bytes(), pay_ident::ident_type::BYRON_KEY };
                    case 2: {
                        auto nested_data = cbor::zero2::parse(w_data.array().read().tag().read().bytes());
                        return pay_ident { nested_data.get().array().read().bytes(), pay_ident::ident_type::BYRON_KEY };
                    }
                    default: throw error(fmt::format("unsupported format of a byron address: {} items", n_items));
                }
            }

            default:
                throw cardano_error(fmt::format("unsupported address for type: {}!", type()));
        }
    }

    template<typename TGT, typename SRC>
    void _apply_one_param_update(TGT &tgt, std::string &desc, const std::optional<SRC> &upd, const std::string_view name)
    {
        if (upd) {
            tgt = static_cast<TGT>(*upd);
            desc += fmt::format("{}: {} ", name, tgt);
        }
    }

    template<>
    void _apply_one_param_update(plutus_cost_models &tgt, std::string &desc, const std::optional<plutus_cost_models> &upd, const std::string_view name)
    {
        if (upd) {
            if (upd->v1)
                tgt.v1 = upd->v1;
            if (upd->v2)
                tgt.v2 = upd->v2;
            if (upd->v3)
                tgt.v3 = upd->v3;
            desc += fmt::format("{}: {} ", name, tgt);
        }
    }

    std::string protocol_params::apply(const param_update &upd)
    {
        std::string update_desc {};
        _apply_one_param_update(protocol_ver, update_desc, upd.protocol_ver, "protocol_ver");
        _apply_one_param_update(min_fee_a, update_desc, upd.min_fee_a, "min_fee_a");
        _apply_one_param_update(min_fee_b, update_desc, upd.min_fee_b, "min_fee_b");
        _apply_one_param_update(max_block_body_size, update_desc, upd.max_block_body_size, "max_block_body_size");
        _apply_one_param_update(max_transaction_size, update_desc, upd.max_transaction_size, "max_transaction_size");
        _apply_one_param_update(max_block_header_size, update_desc, upd.max_block_header_size, "max_block_header_size");
        _apply_one_param_update(key_deposit, update_desc, upd.key_deposit, "key_deposit");
        _apply_one_param_update(pool_deposit, update_desc, upd.pool_deposit, "pool_deposit");
        _apply_one_param_update(e_max, update_desc, upd.e_max, "e_max");
        _apply_one_param_update(n_opt, update_desc, upd.n_opt, "n_opt");
        _apply_one_param_update(pool_pledge_influence, update_desc, upd.pool_pledge_influence, "pool_pledge_influence");
        _apply_one_param_update(expansion_rate, update_desc, upd.expansion_rate, "expansion_rate");
        _apply_one_param_update(treasury_growth_rate, update_desc, upd.treasury_growth_rate, "treasury_growth_rate");
        _apply_one_param_update(decentralization, update_desc, upd.decentralization, "decentralization");
        _apply_one_param_update(extra_entropy, update_desc, upd.extra_entropy, "extra_entropy");
        _apply_one_param_update(min_utxo_value, update_desc, upd.min_utxo_value, "min_utxo_value");
        _apply_one_param_update(min_pool_cost, update_desc, upd.min_pool_cost, "min_pool_cost");
        _apply_one_param_update(lovelace_per_utxo_byte, update_desc, upd.lovelace_per_utxo_byte, "lovelace_per_utxo_byte");
        _apply_one_param_update(ex_unit_prices, update_desc, upd.ex_unit_prices, "ex_unit_prices");
        _apply_one_param_update(max_tx_ex_units, update_desc, upd.max_tx_ex_units, "max_tx_ex_units");
        _apply_one_param_update(max_block_ex_units, update_desc, upd.max_block_ex_units, "max_block_ex_units");
        _apply_one_param_update(max_value_size, update_desc, upd.max_value_size, "max_value_size");
        _apply_one_param_update(max_collateral_pct, update_desc, upd.max_collateral_pct, "max_collateral_pct");
        _apply_one_param_update(max_collateral_inputs, update_desc, upd.max_collateral_inputs, "max_collateral_inputs");
        _apply_one_param_update(plutus_cost_models, update_desc, upd.plutus_cost_models, "plutus_cost_models");
        return update_desc;
    }

    std::string protocol_params::apply(const param_update_t &upd)
    {
        std::string update_desc {};
        _apply_one_param_update(min_fee_a, update_desc, upd.min_fee_a, "min_fee_a");
        _apply_one_param_update(min_fee_b, update_desc, upd.min_fee_b, "min_fee_b");
        _apply_one_param_update(max_block_body_size, update_desc, upd.max_block_body_size, "max_block_body_size");
        _apply_one_param_update(max_transaction_size, update_desc, upd.max_transaction_size, "max_transaction_size");
        _apply_one_param_update(max_block_header_size, update_desc, upd.max_block_header_size, "max_block_header_size");
        _apply_one_param_update(key_deposit, update_desc, upd.key_deposit, "key_deposit");
        _apply_one_param_update(pool_deposit, update_desc, upd.pool_deposit, "pool_deposit");
        _apply_one_param_update(e_max, update_desc, upd.e_max, "e_max");
        _apply_one_param_update(n_opt, update_desc, upd.n_opt, "n_opt");
        _apply_one_param_update(pool_pledge_influence, update_desc, upd.pool_pledge_influence, "pool_pledge_influence");
        _apply_one_param_update(expansion_rate, update_desc, upd.expansion_rate, "expansion_rate");
        _apply_one_param_update(treasury_growth_rate, update_desc, upd.treasury_growth_rate, "treasury_growth_rate");
        _apply_one_param_update(min_pool_cost, update_desc, upd.min_pool_cost, "min_pool_cost");
        _apply_one_param_update(lovelace_per_utxo_byte, update_desc, upd.lovelace_per_utxo_byte, "lovelace_per_utxo_byte");
        _apply_one_param_update(plutus_cost_models, update_desc, upd.plutus_cost_models, "plutus_cost_models");
        _apply_one_param_update(ex_unit_prices, update_desc, upd.ex_unit_prices, "ex_unit_prices");
        _apply_one_param_update(max_tx_ex_units, update_desc, upd.max_tx_ex_units, "max_tx_ex_units");
        _apply_one_param_update(max_block_ex_units, update_desc, upd.max_block_ex_units, "max_block_ex_units");
        _apply_one_param_update(max_value_size, update_desc, upd.max_value_size, "max_value_size");
        _apply_one_param_update(max_collateral_pct, update_desc, upd.max_collateral_pct, "max_collateral_pct");
        _apply_one_param_update(max_collateral_inputs, update_desc, upd.max_collateral_inputs, "max_collateral_inputs");
        _apply_one_param_update(pool_voting_thresholds, update_desc, upd.pool_voting_thresholds, "pool_voting_thresholds");
        _apply_one_param_update(drep_voting_thresholds, update_desc, upd.drep_voting_thresholds, "drep_voting_thresholds");
        _apply_one_param_update(committee_min_size, update_desc, upd.committee_min_size, "committee_min_size");
        _apply_one_param_update(committee_max_term_length, update_desc, upd.committee_max_term_length, "committee_max_term_length");
        _apply_one_param_update(gov_action_lifetime, update_desc, upd.gov_action_lifetime, "gov_action_lifetime");
        _apply_one_param_update(gov_action_deposit, update_desc, upd.gov_action_deposit, "gov_action_deposit");
        _apply_one_param_update(drep_deposit, update_desc, upd.drep_deposit, "drep_deposit");
        _apply_one_param_update(drep_activity, update_desc, upd.drep_activity, "drep_activity");
        _apply_one_param_update(min_fee_ref_script_cost_per_byte, update_desc, upd.min_fee_ref_script_cost_per_byte, "min_fee_ref_script_cost_per_byte");
        return update_desc;
    }

    protocol_version protocol_version::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return { it.read().uint(), it.read().uint() };
    }

    void protocol_version::to_cbor(era_encoder &enc) const
    {
        enc.array(2).uint(major).uint(minor);
    }

    ex_unit_prices ex_unit_prices::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return { decltype(mem)::from_cbor(it.read()), decltype(steps)::from_cbor(it.read()) };
    }

    ex_unit_prices ex_unit_prices::from_json(const json::value &j)
    {
        return { decltype(mem)::from_json(j.at("prMem")), decltype(steps)::from_json(j.at("prSteps")) };
    }

    void ex_unit_prices::to_cbor(era_encoder &enc) const
    {
        enc.array(2);
        mem.to_cbor(enc);
        steps.to_cbor(enc);
    }

    cert_loc_t::cert_loc_t(const uint64_t s, const uint64_t t, const uint64_t c):
        slot { s }, tx_idx { numeric_cast<uint32_t>(t) }, cert_idx { numeric_cast<uint32_t>(c) }
    {
    }

    inline void parse_single_param_update(param_update &upd, const uint64_t idx, cbor::zero2::value &val)
    {
        switch (idx) {
            case 0: upd.min_fee_a.emplace_cbor(val); break;
            case 1: upd.min_fee_b.emplace_cbor(val); break;
            case 2: upd.max_block_body_size.emplace_cbor(val); break;
            case 3: upd.max_transaction_size.emplace_cbor(val); break;
            case 4: upd.max_block_header_size.emplace_cbor(val); break;
            case 5: upd.key_deposit.emplace_cbor(val); break;
            case 6: upd.pool_deposit.emplace_cbor(val); break;
            case 7: upd.e_max.emplace_cbor(val); break;
            case 8: upd.n_opt.emplace_cbor(val); break;
            case 9: upd.pool_pledge_influence.emplace_cbor(val); break;
            case 10: upd.expansion_rate.emplace_cbor(val); break;
            case 11: upd.treasury_growth_rate.emplace_cbor(val); break;
            case 12: upd.decentralization.emplace_cbor(val); break;
            case 13: upd.extra_entropy.emplace_cbor(val); break;
            case 14: upd.protocol_ver.emplace_cbor(val); break;
            case 15: upd.min_utxo_value.emplace_cbor(val); break;
            case 16: upd.min_pool_cost.emplace_cbor(val); break;
            case 17: upd.lovelace_per_utxo_byte.emplace_cbor(val); break;
            case 18: upd.plutus_cost_models.emplace_cbor(val); break;
            case 19: upd.ex_unit_prices.emplace_cbor(val); break;
            case 20: upd.max_tx_ex_units.emplace_cbor(val); break;
            case 21: upd.max_block_ex_units.emplace_cbor(val); break;
            case 22: upd.max_value_size.emplace_cbor(val); break;
            case 23: upd.max_collateral_pct.emplace_cbor(val); break;
            case 24: upd.max_collateral_inputs.emplace_cbor(val); break;
            default: throw error(fmt::format("protocol parameter index is out of the expected range for common params: {}", idx));
        }
    }

    param_update param_update::from_cbor(cbor::zero2::value &v)
    {
        param_update upd {};
        auto &it = v.map();
        while (!it.done()) {
            auto &id = it.read_key();
            const auto idx = id.uint();
            parse_single_param_update(upd, idx, it.read_val(std::move(id)));
        }
        return upd;
    }

    stake_pointer stake_pointer::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return { it.read().uint(), it.read().uint(), it.read().uint() };
    }

    void stake_pointer::to_cbor(era_encoder &enc) const
    {
        enc.array(3).uint(slot).uint(tx_idx).uint(cert_idx);
    }

    credential_t credential_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        const auto script = it.read().uint() == 1;
        return { it.read().bytes(), script };
    }

    credential_t credential_t::from_json(const std::string_view s)
    {
        const auto pos = s.find('-');
        if (pos == std::string::npos) [[unlikely]]
            throw error(fmt::format("invalid credential format: {}", s));
        const auto typ = s.substr(0, pos);
        const auto hex = s.substr(pos + 1);
        bool script;
        if (typ == "keyHash") {
            script = false;
        } else if (typ == "scriptHash") {
            script = true;
        } else {
            throw error(fmt::format("invalid credential format: {}", s));
        }
        return { key_hash::from_hex(hex), script };
    }

    void credential_t::to_cbor(era_encoder &enc) const
    {
        enc.array(2).uint(script ? 1 : 0).bytes(hash);
    }

    pool_metadata pool_metadata::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return { std::string { it.read().text() }, it.read().bytes() };
    }

    void pool_metadata::to_cbor(era_encoder &enc) const
    {
        enc.array(2)
            .text(url)
            .bytes(hash);
    }

    pool_voting_thresholds_t pool_voting_thresholds_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return {
            decltype(motion_of_no_confidence)::from_cbor(it.read()),
            decltype(committee_normal)::from_cbor(it.read()),
            decltype(committee_no_confidence)::from_cbor(it.read()),
            decltype(hard_fork_initiation)::from_cbor(it.read()),
            decltype(security_voting_threshold)::from_cbor(it.read()),
        };
    }

    pool_voting_thresholds_t pool_voting_thresholds_t::from_json(const json::value &j)
    {
        return {
            decltype(motion_of_no_confidence)::from_json(j.at("motionNoConfidence")),
            decltype(committee_normal)::from_json(j.at("committeeNormal")),
            decltype(committee_no_confidence)::from_json(j.at("committeeNoConfidence")),
            decltype(hard_fork_initiation)::from_json(j.at("hardForkInitiation")),
            decltype(security_voting_threshold)::from_json(j.at("ppSecurityGroup"))
        };
    }

    void pool_voting_thresholds_t::to_cbor(era_encoder &enc) const
    {
        enc.array(5);
        motion_of_no_confidence.to_cbor(enc);
        committee_normal.to_cbor(enc);
        committee_no_confidence.to_cbor(enc);
        hard_fork_initiation.to_cbor(enc);
        security_voting_threshold.to_cbor(enc);
    }

    drep_voting_thresholds_t drep_voting_thresholds_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return {
            decltype(motion_no_confidence)::from_cbor(it.read()),
            decltype(committee_normal)::from_cbor(it.read()),
            decltype(committee_no_confidence)::from_cbor(it.read()),
            decltype(update_constitution)::from_cbor(it.read()),
            decltype(hard_fork_initiation)::from_cbor(it.read()),
            decltype(pp_network_group)::from_cbor(it.read()),
            decltype(pp_economic_group)::from_cbor(it.read()),
            decltype(pp_technical_group)::from_cbor(it.read()),
            decltype(pp_governance_group)::from_cbor(it.read()),
            decltype(treasury_withdrawal)::from_cbor(it.read()),
        };
    }

    drep_voting_thresholds_t drep_voting_thresholds_t::from_json(const json::value &j)
    {
        return {
            decltype(motion_no_confidence)::from_json(j.at("motionNoConfidence")),
            decltype(committee_normal)::from_json(j.at("committeeNormal")),
            decltype(committee_no_confidence)::from_json(j.at("committeeNoConfidence")),
            decltype(update_constitution)::from_json(j.at("updateToConstitution")),
            decltype(hard_fork_initiation)::from_json(j.at("hardForkInitiation")),
            decltype(pp_network_group)::from_json(j.at("ppNetworkGroup")),
            decltype(pp_economic_group)::from_json(j.at("ppEconomicGroup")),
            decltype(pp_technical_group)::from_json(j.at("ppTechnicalGroup")),
            decltype(pp_governance_group)::from_json(j.at("ppGovGroup")),
            decltype(treasury_withdrawal)::from_json(j.at("treasuryWithdrawal"))
        };
    }

    void drep_voting_thresholds_t::to_cbor(era_encoder &enc) const
    {
        enc.array(10);
        motion_no_confidence.to_cbor(enc);
        committee_normal.to_cbor(enc);
        committee_no_confidence.to_cbor(enc);
        update_constitution.to_cbor(enc);
        hard_fork_initiation.to_cbor(enc);
        pp_network_group.to_cbor(enc);
        pp_economic_group.to_cbor(enc);
        pp_technical_group.to_cbor(enc);
        pp_governance_group.to_cbor(enc);
        treasury_withdrawal.to_cbor(enc);
    }

    void plutus_cost_model::to_cbor(era_encoder &enc) const
    {
        enc.array_compact(size(), [&] {
            for (const auto &[name, cost]: *this) {
                if (cost >= 0)
                    enc.uint(cost);
                else
                    enc.nint(-(cost + 1));
            }
        });
    }

    void plutus_cost_models::to_cbor(era_encoder &enc) const
    {
        auto l_enc { enc };
        size_t cnt = 0;
        for (auto &[id, m]:
                std::initializer_list<std::pair<size_t, const std::optional<plutus_cost_model> &>>{{ 0, v1 }, { 1, v2 }, { 2, v3} }) {
            if (m) {
                l_enc.uint(id);
                m->to_cbor(l_enc);
                ++cnt;
            }
        }
        if (!cnt) [[unlikely]]
            throw error("a plutus_cost_model structure must have at least one model defined!");
        enc.map_compact(cnt, [&] {
            enc << l_enc;
        });
    }

    void slot::to_cbor(era_encoder &enc) const
    {
        enc.array(3);
        big_int_to_cbor(enc, cpp_int { unixtime() - _cfg.byron_start_time } * 1'000'000'000'000);
        enc.uint(_slot);
        enc.uint(epoch());
    }

    static void assets_to_cbor(era_encoder &enc, const tx_out_data &data)
    {
        if (!data.assets.empty()) {
            enc.array(2);
            enc.uint(data.coin);
            data.assets.to_cbor(enc);
        } else {
            enc.uint(data.coin);
        }
    }

    void tx_out_data::to_cbor(era_encoder &enc) const
    {
        if (script_ref || (datum && datum->val.index() != 0)) {
            enc.map(2 + (datum ? 1 : 0) + (script_ref ? 1 : 0));
            enc.uint(0);
            addr().to_cbor(enc);
            enc.uint(1);
            assets_to_cbor(enc, *this);
            if (datum) {
                enc.uint(2);
                enc.array(2);
                switch (const auto typ = datum->val.index(); typ) {
                    case 0: {
                        enc.uint(0);
                        enc.bytes(std::get<datum_hash>(datum->val));
                        break;
                    }
                    case 1: {
                        enc.uint(1);
                        enc.tag(24);
                        enc.bytes(std::get<uint8_vector>(datum->val));
                        break;
                    }
                    default:
                        throw error(fmt::format("unsupported tx_out_data::datum_option_type index: {}", typ));
                }
            }
            if (script_ref) {
                enc.uint(3);
                script_ref->to_cbor(enc);
            }
        } else {
            enc.array(2 + (datum ? 1 : 0));
            addr().to_cbor(enc);
            assets_to_cbor(enc, *this);
            if (datum)
                enc.bytes(std::get<datum_hash>(datum->val));
        }
    }

    plutus_cost_model plutus_cost_model::from_cbor(const vector<std::string> &names, cbor::zero2::value &v)
    {
        plutus_cost_model res {};
        res.reserve(names.size());
        auto &it = v.array();
        for (size_t i = 0; !it.done() && i < names.size(); ++i) {
            auto &val = it.read();
            switch (const auto typ = val.type(); typ) {
                case cbor::major_type::uint: res.emplace_back(names[i], numeric_cast<int64_t>(val.uint())); break;
                case cbor::major_type::nint: res.emplace_back(names[i], -numeric_cast<int64_t>(val.nint())); break;
                default: throw error(fmt::format("unsupported plutus_cost_model value type: {}", typ));
            }
        }
        return res;
    }

    plutus_cost_model plutus_cost_model::from_json(const plutus_cost_model &orig, const json::value &data)
    {
        plutus_cost_model res {};
        res.reserve(orig.size());
        if (data.is_object()) {
            const auto &data_obj = data.as_object();
            if (orig.size() != data_obj.size())
                throw error(fmt::format("was expecting an array with {} elements but got {}", orig.size(), data_obj.size()));
            for (size_t i = 0; i < orig.size(); ++i) {
                const auto &key = orig.storage().at(i).first;
                auto it = data_obj.find(key);
                if (it == data_obj.end())
                    it = data_obj.find(plutus::costs::v1_arg_name(key));
                if (it == data_obj.end())
                    throw error(fmt::format("missing required cost model key: {}", key));
                res.emplace_back(key, json::value_to<int64_t>(it->value()));
            }
        } else if (data.is_array()) {
            const auto &data_arr = data.as_array();
            if (orig.size() != data_arr.size())
                throw error(fmt::format("was expecting an array with {} elements but got {}", orig.size(), data_arr.size()));
            for (size_t i = 0; i < orig.size(); ++i) {
                const auto &key = orig.storage().at(i).first;
                res.emplace_back(key, json::value_to<int64_t>(data_arr[i]));
            }
        } else {
            throw error(fmt::format("an unsupported json value representing a cost model: {}", json::serialize_pretty(data)));
        }
        return res;
    }

    void plutus_cost_model::update(const plutus_cost_model &src)
    {
        for (auto &item: _data) {
            if (const auto it = src.find(item.first); it != src.end())
                item.second = it->second;
        }
    }

    plutus_cost_model::diff_type plutus_cost_model::diff(const plutus_cost_model &o) const
    {
        diff_type m {};
        for (const auto &[k, v]: *this) {
            const auto it = o.find(k);
            if (it == o.end()) {
                m.try_emplace(k, v, std::optional<int64_t> {});
            } else if (it->second != v) {
                m.try_emplace(k, v, it->second);
            }
        }
        for (const auto &[k, v]: o) {
            const auto it = find(k);
            if (it == end())
                m.try_emplace(k, std::optional<int64_t> {}, v);
        }
        return m;
    }

    slot slot::from_time(const std::chrono::time_point<std::chrono::system_clock> &tp, const cardano::config &cfg)
    {
        const uint64_t secs = std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count();
        if (secs >= cfg.byron_start_time) [[likely]] {
            if (secs >= cfg.shelley_start_time()) [[likely]]
                return { secs - cfg.shelley_start_time() + cfg.shelley_start_slot(), cfg };
            return { (secs - cfg.byron_start_time) / cfg.byron_slot_duration, cfg };
        }
        throw error(fmt::format("cannot create a slot from a time point before the byron start time: {}", cfg.byron_start_time));
    }

    slot slot::from_epoch(const uint64_t epoch, const uint64_t epoch_slot, const cardano::config &cfg)
    {
        if (epoch >= cfg.shelley_start_epoch()) [[likely]]
            return { (epoch - cfg.shelley_start_epoch()) * cfg.shelley_epoch_length + cfg.shelley_start_slot() + epoch_slot, cfg };
        return { epoch * cfg.byron_epoch_length + epoch_slot, cfg };
    }

    slot slot::from_chunk(const uint64_t chunk, const cardano::config &cfg)
    {
        return { chunk * cfg.byron_epoch_length, cfg };
    }

    slot slot::from_epoch(const uint64_t epoch, const cardano::config &cfg)
    {
        return from_epoch(epoch, 0, cfg);
    }

    slot slot::from_future(const cardano::config &cfg)
    {
        return from_time(std::chrono::system_clock::now() + std::chrono::seconds { 5 }, cfg);
    }

    uint64_t slot::epoch() const
    {
        if (_slot > _cfg.shelley_start_slot())
            return _cfg.shelley_start_epoch() + (_slot - _cfg.shelley_start_slot()) / _cfg.shelley_epoch_length;
        return _slot / _cfg.byron_epoch_length;
    }

    uint64_t slot::epoch_slot() const
    {
        if (_slot > _cfg.shelley_start_slot())
            return (_slot - _cfg.shelley_start_slot()) % _cfg.shelley_epoch_length;
        return _slot % _cfg.byron_epoch_length;
    }

    uint64_t slot::chunk_id() const
    {
        return _slot / _cfg.byron_epoch_length;
    }

    uint64_t slot::unixtime() const
    {
        if (_slot >= _cfg.shelley_start_slot())
            return _cfg.shelley_start_time() + (_slot - _cfg.shelley_start_slot());
        return _cfg.shelley_start_time() - (_cfg.shelley_start_slot() - _slot) * 20;
    }

    std::string slot::timestamp() const
    {
        static mutex::unique_lock::mutex_type gmtime_mutex alignas(mutex::alignment) {};
        std::stringstream ss {};
        std::time_t t = unixtime();
        {
            mutex::scoped_lock lk { gmtime_mutex };
            std::tm* tm = std::gmtime(&t);
            ss << std::put_time(tm, "%Y-%m-%d %H:%M:%S");
        }
        return ss.str();
    }

    std::string slot::utc_month() const
    {
        return timestamp().substr(0, 7);
    }

    void param_update::rehash()
    {
        memset(hash.data(), 0, hash.size());
        blake2b(hash, zpp::serialize(*this));
    }

    plutus_cost_models plutus_cost_models::from_cbor(cbor::zero2::value &v)
    {
        plutus_cost_models res {};
        for (auto &it = v.map(); !it.done(); ) {
            auto &key_v = it.read_key();
            const auto typ = key_v.uint();
            auto &val_v = it.read_val(std::move(key_v));
            switch (typ) {
                case 0:
                    res.v1.emplace(plutus_cost_model::from_cbor(plutus::costs::cost_arg_names_v1(), val_v));
                    if (res.v1->size() != plutus::costs::cost_arg_names_v1().size()) [[unlikely]]
                        throw error(fmt::format("an unexpected number of plutus v1 cost model arguments: {}", res.v1->size()));
                    break;
                case 1:
                    res.v2.emplace(plutus_cost_model::from_cbor(plutus::costs::cost_arg_names_v2(), val_v));
                    if (res.v2->size() != plutus::costs::cost_arg_names_v2().size()) [[unlikely]]
                        throw error(fmt::format("an unexpected number of plutus v2 cost model arguments: {}", res.v2->size()));
                    break;
                case 2:
                    res.v3.emplace(plutus_cost_model::from_cbor(plutus::costs::cost_arg_names_v3(), val_v));
                    if (res.v3->size() != plutus::costs::cost_arg_names_v3().size() && res.v3->size() != 251) [[unlikely]]
                        throw error(fmt::format("an unexpected number of plutus v3 cost model arguments: {}", res.v3->size()));
                    break;
                default: throw error(fmt::format("unsupported cost model id: {}", typ));
            }
        }
        return res;
    }

    datum_option_t datum_option_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        switch (const auto id = it.read().uint(); id) {
            case 0: return { datum_hash { it.read().bytes() } };
            case 1: return { uint8_vector { it.read().tag().read().bytes() } };
            default: throw error(fmt::format("unsupported datum_option id: {}", id));
        }
    }

    output_value_t output_value_t::from_cbor(cbor::zero2::value &v)
    {
        switch (const auto typ = v.type(); typ) {
            case cbor::major_type::uint: return { v.uint() };
            case cbor::major_type::array: {
                auto &it = v.array();
                const auto coin = it.read().uint();
                auto &assets_v = it.read();
                auto &a_it = assets_v.map();
                multi_asset_map assets {};
                if (!assets_v.indefinite())
                    assets.reserve(assets_v.special_uint());
                while (!a_it.done()) {
                    auto &policy_id_v = a_it.read_key();
                    const auto policy_id_bytes = policy_id_v.bytes();
                    auto &p_assets_v = a_it.read_val(std::move(policy_id_v));
                    auto &p_it = p_assets_v.map();
                    policy_asset_map p_assets {};
                    if (!p_assets_v.indefinite())
                        p_assets.reserve(p_assets_v.special_uint());
                    while (!p_it.done()) {
                        auto &name_v = p_it.read_key();
                        const auto name_bytes = name_v.bytes();
                        if (const auto asset_coin = p_it.read_val(std::move(name_v)).uint(); asset_coin) [[likely]]
                            p_assets.emplace_hint(p_assets.end(), name_bytes, asset_coin);
                    }
                    if (!p_assets.empty()) [[likely]]
                        assets.emplace_hint(assets.end(), policy_id_bytes, std::move(p_assets));
                }
                return { coin, std::move(assets) };
            }
            default: throw error(fmt::format("unsupported output value type: {}", typ));
        }
    }

    static uint8_vector addr_bytes_from_cbor(cbor::zero2::value &v)
    {
        switch (const auto typ = v.type(); typ) {
            case cbor::major_type::array: return { v.array().read().tag().read().bytes() };
            case cbor::major_type::bytes: return { v.bytes() };
            [[unlikely]] default: throw error(fmt::format("unsupported address CBOR type: {}", typ));
        }
    }

    tx_out_data tx_out_data::from_shelley_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        auto addr_bytes = addr_bytes_from_cbor(it.read());
        auto value = output_value_t::from_cbor(it.read());
        tx_out_data txo { addr_bytes, value.coin, std::move(value.assets) };
        if (!it.done())
            txo.datum.emplace(datum_hash { it.read().bytes() });
        return txo;
    }

    tx_out_data tx_out_data::from_babbage_cbor(cbor::zero2::value &v)
    {
        auto &it = v.map();
        std::optional<uint8_vector> addr {};
        std::optional<output_value_t> value {};
        std::optional<datum_option_t> datum {};
        std::optional<script_info> script_ref {};
        while (!it.done()) {
            auto &id_v = it.read_key();
            const auto id = id_v.uint();
            auto &val = it.read_val(std::move(id_v));
            switch (id) {
                case 0: addr.emplace(val.bytes()); break;
                case 1: value.emplace(output_value_t::from_cbor(val)); break;
                case 2: datum.emplace(datum_option_t::from_cbor(val)); break;
                case 3: script_ref.emplace(script_info::from_cbor(val.tag().read().bytes())); break;
                default: throw error(fmt::format("unsupported tx_output id: {}", id));
            }
        }
        if (!addr) [[unlikely]]
            throw error(fmt::format("tx_output must contain an address but got: {}", v.to_string()));
        if (!value) [[unlikely]]
            throw error(fmt::format("tx_output must contain a value but got: {}", v.to_string()));
        return { *addr, value->coin, std::move(value->assets), datum, script_ref };
    }

    tx_out_data tx_out_data::from_cbor(cbor::zero2::value &v)
    {
        switch (const auto typ = v.type(); typ) {
            case cbor::major_type::array: return from_shelley_cbor(v);
            case cbor::major_type::map: return from_babbage_cbor(v);
            default: throw error(fmt::format("unsupported cbor type in tx_output: {}", typ));
        }
    }

    script_info script_info::from_cbor(const script_type typ, cbor::zero2::value &v)
    {
        switch (typ) {
            case script_type::native: return { typ, v.data_raw() };
            default: return { typ, v.bytes() };
        }
    }

    script_info script_info::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        const auto s_typ = script_type_from_cbor(it.read());
        return from_cbor(s_typ, it.read());
    }

    script_info script_info::from_cbor(const buffer bytes)
    {
        return from_cbor(cbor::zero2::parse(bytes).get());
    }

    void script_info::to_cbor(era_encoder &enc) const
    {
        enc.tag(24);
        auto s_enc { enc };
        s_enc.array(2);
        s_enc.uint(_data[0]);
        switch (const auto typ = type(); typ) {
            case script_type::native:
                s_enc.raw_cbor(script());
                break;
            default:
                s_enc.bytes(script());
                break;
        }
        enc.bytes(s_enc.cbor());
    }

    ex_units ex_units::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return {
            it.read().uint(),
            it.read().uint()
        };
    }

    ex_units ex_units::from_json(const json::value &j)
    {
        return {
            json::value_to<uint64_t>(j.at("exUnitsMem")),
            json::value_to<uint64_t>(j.at("exUnitsSteps"))
        };
    }

    void ex_units::to_cbor(era_encoder &enc) const
    {
        enc.array(2);
        enc.uint(mem);
        enc.uint(steps);
    }

    drep_t drep_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        switch (const auto dtyp = it.read().uint(); dtyp) {
            case 0: return drep_t { credential_t { it.read().bytes(), false } };
            case 1: return drep_t { credential_t { it.read().bytes(), true } };
            case 2: return drep_t { abstain_t {} };
            case 3: return drep_t { no_confidence_t {} };
            default: throw error(fmt::format("unsupported drep type: {}", dtyp));
        }
    }

    void drep_t::to_cbor(era_encoder &enc) const
    {
        std::visit([&](const auto &c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, credential_t>) {
                enc.array(2).uint(c.script ? 1 : 0);
                enc.bytes(c.hash);
            } else if constexpr (std::is_same_v<T, abstain_t>) {
                enc.array(1).uint(2);
            } else if constexpr (std::is_same_v<T, no_confidence_t>) {
                enc.array(1).uint(3);
            } else {
                throw error(fmt::format("unsupported drep type: {}", typeid(T).name()));
            }
        }, val);
    }

    std::tuple<uint8_t, size_t> from_haskell_char(const std::string_view sv)
    {
        static std::map<uint8_t, uint8_t> one_char_codes {
            { '0', 0x00 }, { 'a', 0x07 }, { 'b', 0x08 }, { 'f', 0x0C },
            { 'n', 0x0A }, { 'r', 0x0D }, { 't', 0x09 }, { 'v', 0x0B },
            { '"', 0x22 }, { '\'', 0x27 }, { '\\', 0x5C }
        };
        static std::map<std::string, uint8_t> multichar_codes {
            { "BS"s, 0x08 }, { "HT"s, 0x09 }, { "LF"s, 0x0A }, { "VT"s, 0x0B },
            { "FF"s, 0x0C }, { "CR"s, 0x0D }, { "SO"s, 0x0E }, { "SI"s, 0x0F },
            { "EM"s, 0x19 }, { "FS"s, 0x1C }, { "GS"s, 0x1D }, { "RS"s, 0x1E },
            { "US"s, 0x1F }, { "SP"s, 0x20 },
            
            // SO and SOH share the same prefix, so the resolution should go from longest to shortest matches!
            { "NUL"s, 0x00 }, { "SOH"s, 0x01 }, { "STX"s, 0x02 }, { "ETX"s, 0x03 },
            { "EOT"s, 0x04 }, { "ENQ"s, 0x05 }, { "ACK"s, 0x06 }, { "BEL"s, 0x07 },            
            { "DLE"s, 0x10 }, { "DC1"s, 0x11 }, { "DC2"s, 0x12 }, { "DC3"s, 0x13 },
            { "DC4"s, 0x14 }, { "NAK"s, 0x15 }, { "SYN"s, 0x16 }, { "ETB"s, 0x17 },
            { "CAN"s, 0x18 }, { "SUB"s, 0x1A }, { "ESC"s, 0x1B }, { "DEL"s, 0x7F }
        };
        if (sv[0] >= '1' && sv[0] <= '9') {
            auto end = sv.find_first_not_of("0123456789"sv);
            if (end == std::string_view::npos) end = sv.size();
            std::string text { sv.substr(0, end) };
            uint8_t byte = std::stoul(text);
            return std::make_tuple(byte, end);
        } else if (sv[0] >= 'A' && sv[0] <= 'Z') {
            for (size_t n_chars = sv.size() > 3 ? 3 : sv.size(); n_chars >= 1; --n_chars) {
                std::string text { sv.substr(0, n_chars) };
                auto it = multichar_codes.find(text);
                if (it != multichar_codes.end()) {
                    return std::make_tuple(it->second, n_chars);
                }
            }
            throw error(fmt::format("Unsupported escape sequence starting with {}!", sv));
        } else {
            auto it = one_char_codes.find(sv[0]);
            if (it != one_char_codes.end()) {
                return std::make_tuple(it->second, 1);
            }
            throw error(fmt::format("Escape sequence starts from an unsupported character: '{}' code {}!", sv[0], (int)sv[0]));
        }
    }

    uint8_vector from_haskell(const std::string_view sv)
    {
        uint8_vector bytes;
        for (size_t i = 0; i < sv.size(); ++i) {
            if (sv[i] != '\\') {
                bytes.push_back(sv[i]);
            } else if (i + 1 < sv.size()) {
                if (sv[i + 1] != '&') {
                    const auto [byte, extra_size] = from_haskell_char(sv.substr(i + 1));
                    bytes.push_back(byte);
                    i += extra_size;
                } else {
                    // empty string, just skip it
                    i += 1;
                }
            }
        }
        return bytes;
    }

    byron_addr byron_addr::from_bytes(const buffer bytes)
    {
        auto pv = cbor::zero2::parse(bytes);
        auto &v = pv.get();
        switch (v.special_uint()) {
            case 2: {
                auto pv2 = cbor::zero2::parse(v.array().read().tag().read().bytes());
                return { pv2.get().array(), v };
            }
            case 3: return { v.array(), v };
            default: throw error(fmt::format("Unsupported byron address format {}!", v.special_uint()));
        }
    }

    byron_addr::byron_addr(cbor::zero2::array_reader &it, cbor::zero2::value &v):
        _root { it.read().bytes() },
        _attrs { it.read().data_raw() },
        _type { numeric_cast<uint8_t>(it.read().uint()) },
        _bytes { v.data_raw() }
    {
    }

    bool byron_addr::vkey_ok(const buffer vk, const uint8_t typ) const
    {
        const auto root_hash = byron_addr_root_hash(type(), vk, attrs());
        return root_hash == root() && typ == type();
    }

    key_hash byron_addr::bootstrap_root(cbor::zero2::value &w_data) const
    {
        auto &it = w_data.array();
        uint8_vector vk_full {};
        vk_full << it.read().bytes() << it.skip(1).read().bytes();
        return byron_addr_root_hash(0, vk_full, it.read().bytes());
    }

    bool byron_addr::bootstrap_ok(cbor::zero2::value &w_data) const
    {
        const auto root_hash = bootstrap_root(w_data);
        return root_hash == root() && 0 == type();
    }

    vrf_cert vrf_cert::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return {
            it.read().bytes(),
            it.read().bytes(),
        };
    }

    operational_cert operational_cert::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return {
            it.read().bytes(),
            it.read().uint(),
            it.read().uint(),
            it.read().bytes(),
        };
    }

    relay_addr relay_addr::from_cbor(cbor::zero2::array_reader &it)
    {
        return { decltype(port)::from_cbor(it.read()), decltype(ipv4)::from_cbor(it.read()), decltype(ipv6)::from_cbor(it.read()) };
    }

    void relay_addr::to_cbor(era_encoder &enc) const
    {
        enc.array(4);
        enc.uint(0);
        port.to_cbor(enc);
        ipv4.to_cbor(enc);
        ipv6.to_cbor(enc);
    }

    relay_host relay_host::from_cbor(cbor::zero2::array_reader &it)
    {
        return { decltype(port)::from_cbor(it.read()), std::string { it.read().text() } };
    }

    void relay_host::to_cbor(era_encoder &enc) const
    {
        enc.array(3);
        enc.uint(1);
        port.to_cbor(enc);
        enc.text(host);
    }

    relay_dns relay_dns::from_cbor(cbor::zero2::array_reader &it)
    {
        return { std::string { it.read().text() } };
    }

    void relay_dns::to_cbor(era_encoder &enc) const
    {
        enc.array(2);
        enc.uint(2);
        enc.text(name);
    }

    relay_info relay_info::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        switch (const auto typ = it.read().uint(); typ) {
            case 0: return { relay_addr::from_cbor(it) };
            case 1: return { relay_host::from_cbor(it) };
            case 2: return { relay_dns::from_cbor(it) };
            default: throw error(fmt::format("Unsupported relay address format {}!", typ));
        }
    }

    void relay_info::to_cbor(era_encoder &enc) const
    {
        std::visit([&](const auto &v) {
            v.to_cbor(enc);
        }, val);
    }

    bool relay_info::operator==(const relay_info &o) const
    {
        return val == o.val;
    }

    reward_id_t reward_id_t::from_cbor(cbor::zero2::value &v)
    {
        return { v.bytes() };
    }

    stake_keyhash_t stake_keyhash_t::from_cbor(cbor::zero2::value &v)
    {
        return { v.bytes() };
    }

    asset_name_t::asset_name_t(const buffer bytes)
    {
        if (bytes.size() > _data.size()) [[unlikely]]
            throw error(fmt::format("asset names must have 32 bytes max but got {}!", bytes.size()));
        memcpy(_data.data(), bytes.data(), bytes.size());
        _size = numeric_cast<uint8_t>(bytes.size());
    }

    void asset_name_t::to_cbor(era_encoder &enc) const
    {
        enc.bytes(span());
    }

    std::string asset_name_t::to_string(const script_hash &policy_id) const
    {
        return fmt::format("{} {}", buffer_readable { span() }, policy_id);
    }

    json::object multi_asset_map::to_json(const size_t offset, const size_t max_items) const
    {
        json::object j {};
        size_t i = 0;
        size_t cnt = 0;
        for (const auto &[policy_id, assets]: *this) {
            for (const auto &[asset_name, amount]: assets) {
                if (i++ >= offset) {
                    j.emplace(asset_name.to_string(policy_id), amount);
                    if (++cnt >= max_items)
                        return j;
                }
            }
        }
        return j;
    }

    point point::from_ledger_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        const auto slot = it.read().uint();
        const auto height = it.read().uint();
        return { it.read().bytes(), slot, height };
    }

    point point::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        const auto slot = it.read().uint();
        return { it.read().bytes(), slot, it.read().uint() };
    }

    key_hash byron_addr_root_hash(const size_t typ, const buffer vk, const buffer attrs_cbor) {
        cbor::encoder enc {};
        enc.array(3);
        enc.uint(typ);
        enc.array(2);
        enc.uint(typ);
        enc.bytes(vk);
        enc.raw_cbor(attrs_cbor);
        return blake2b<key_hash>(sha3::digest(enc.cbor()));
    }

    inline uint8_vector byron_encode_redeem_root(const buffer redeem_vk)
    {
        cbor::encoder enc {};
        enc.array(3)
            .uint(2)
            .array(2).uint(2).bytes(redeem_vk)
            .map(0);
        return enc.cbor();
    }

    inline key_hash byron_address_hash(const buffer data)
    {
        return blake2b<key_hash>(sha3::digest(data));
    }

    inline uint8_vector byron_encode_address(const buffer root_hash)
    {
        cbor::encoder enc {};
        enc.array(3)
            .bytes(root_hash)
            .map(0)
            .uint(2);
        return enc.cbor();
    }

    uint8_vector byron_crc_protected(const buffer &encoded_addr)
    {
        cbor::encoder enc {};
        enc.array(2);
        enc.tag(24).bytes(encoded_addr);
        enc.uint(crc32::digest(encoded_addr));
        return enc.cbor();
    }

    uint8_vector byron_avvm_addr(std::string_view redeem_vk_base64u)
    {
        const auto redeem_vk = base64::decode_url(redeem_vk_base64u);
        const auto encoded_root = byron_encode_redeem_root(redeem_vk);
        const auto root_hash = byron_address_hash(encoded_root);
        const auto encoded_addr = byron_encode_address(root_hash);
        return byron_crc_protected(encoded_addr);
    }

    tx_hash byron_avvm_tx_hash(std::string_view redeem_vk)
    {
        return blake2b<tx_hash>(byron_avvm_addr(redeem_vk));
    }
}