/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/base64.hpp>
#include <dt/cardano/common/config.hpp>
#include <dt/plutus/costs.hpp>

namespace daedalus_turbo::cardano {
    const config &config::get()
    {
        static config c { configs_dir::get() };
        return c;
    }

    shelley_delegate_map config::_shelley_prep_delegates(const daedalus_turbo::config &shelley_genesis)
    {
        shelley_delegate_map delegs {};
        for (const auto &[id, meta]: shelley_genesis.at("genDelegs").as_object()) {
            delegs[key_hash::from_hex(id)] = shelley_delegate {
                pool_hash::from_hex(meta.at("delegate").as_string()),
                vrf_vkey::from_hex(meta.at("vrf").as_string())
            };
        }
        return delegs;
    }

    txo_map config::_byron_prep_utxos(const daedalus_turbo::config &byron_genesis)
    {
        txo_map txos {};
        for (const auto &[redeem_key, lovelace]: byron_genesis.at("avvmDistr").as_object()) {
            const auto txo_addr = byron_avvm_addr(redeem_key);
            tx_out_ref txo_id { blake2b<tx_hash>(txo_addr), 0 };
            tx_out_data txo_data { std::move(txo_addr), std::stoull(json::value_to<std::string>(lovelace)) };
            if (const auto [it, created] = txos.try_emplace(txo_id, std::move(txo_data)); !created) [[unlikely]]
                throw error(fmt::format("duplicate TXO {} in the byron genesis config", txo_id));
        }
        return txos;
    }

    set<vkey> config::_byron_prep_heavy(const daedalus_turbo::config &genesis, const std::string_view key)
    {
        set<vkey> issuers {};
        for (const auto &[deleg_id, deleg_info]: genesis.at("heavyDelegation").as_object()) {
            issuers.emplace(static_cast<buffer>(base64::decode(json::value_to<std::string_view>(deleg_info.at(key)))).subbuf(0, 32));
        }
        return issuers;
    }

    set<key_hash> config::_byron_prep_hashes(const set<vkey> &vkeys)
    {
        set<key_hash> hashes {};
        for (const auto &vk: vkeys) {
            hashes.emplace(blake2b<key_hash>(vk));
        }
        return hashes;
    }

    block_hash config::_verify_hash_byron(const std::string_view &hash_hex, const daedalus_turbo::config &genesis)
    {
        const auto cfg_hash = block_hash::from_hex(hash_hex);
        const auto cfg_canon = json::serialize_canon(genesis.json());
        auto act_hash = blake2b<block_hash>(cfg_canon);
        if (act_hash != cfg_hash)
            throw error("The actual hash of ByronGenesisFile does not match ByronGenesisHash!");
        return act_hash;
    }

    block_hash config::_verify_hash(const std::string_view &hash_hex, const daedalus_turbo::config &genesis)
    {
        const auto cfg_hash = block_hash::from_hex(hash_hex);
        auto act_hash = blake2b<block_hash>(genesis.bytes());
        if (act_hash != cfg_hash)
            throw error(fmt::format("The actual hash of genesis file does not match {}!", hash_hex));
        return act_hash;
    }

    static plutus_cost_model _make_plutus_v1_default_cost_model()
    {
        const auto &names = plutus::costs::cost_arg_names_v1();
        const auto &d_args = plutus::costs::default_cost_args_v1();
        plutus_cost_model costs {};
        for (const auto &name: names)
            costs.emplace_back(name, std::stoll(d_args.at(plutus::costs::canonical_arg_name(name))));
        if (costs.size() != 166) [[unlikely]]
            throw error("internal error: plutus v1 default costs are invalid!");
        costs.sort();
        return costs;
    }

    static plutus_cost_model _make_plutus_v2_default_cost_model()
    {
        const auto &names = plutus::costs::cost_arg_names_v2();
        const auto &d_args = plutus::costs::default_cost_args_v2();
        plutus_cost_model costs {};
        for (const auto &name: names)
            costs.emplace_back(name, std::stoll(d_args.at(plutus::costs::canonical_arg_name(name))));
        if (costs.size() != 175) [[unlikely]]
            throw error("internal error: plutus v2 default costs are invalid!");
        costs.sort();
        return costs;
    }

    static plutus_cost_model _make_plutus_v3_default_cost_model()
    {
        const auto &names = plutus::costs::cost_arg_names_v3();
        const auto &d_args = plutus::costs::default_cost_args_v3();
        plutus_cost_model costs {};
        for (size_t i = 0; i < names.size() && i < 251; ++i) {
            const auto &name = names[i];
            costs.emplace_back(name, std::stoll(d_args.at(plutus::costs::canonical_arg_name(name))));
        }
        if (costs.size() != 251) [[unlikely]]
            throw error("internal error: plutus v3 default costs are invalid!");
        costs.sort();
        return costs;
    }

    plutus_cost_models config::_prep_plutus_cost_models(const daedalus_turbo::config &genesis)
    {
        static plutus_cost_model v1_defaults = _make_plutus_v1_default_cost_model();
        static plutus_cost_model v2_defaults = _make_plutus_v2_default_cost_model();
        static plutus_cost_model v3_defaults = _make_plutus_v3_default_cost_model();
        plutus_cost_models res {};
        const auto &cfg_models = genesis.at("costModels").as_object();
        const auto import = [&](const std::string &param, const plutus_cost_model &defaults) {
            const auto it = cfg_models.find(param);
            return it != cfg_models.end() ? plutus_cost_model::from_json(defaults, it->value()) : defaults;
        };
        res.v1.emplace(import("PlutusV1", v1_defaults));
        res.v2.emplace(import("PlutusV2", v2_defaults));
        res.v3.emplace(import("PlutusV3", v3_defaults));
        return res;
    }

    config::config(const configs &cfg)
        : byron_genesis { cfg.at(std::filesystem::path { json::value_to<std::string>(cfg.at("config").at("ByronGenesisFile")) }.stem().string()) },
        byron_genesis_hash { _verify_hash_byron(cfg.at("config").at("ByronGenesisHash").as_string(), byron_genesis) },
        byron_protocol_magic { json::value_to<uint32_t>(byron_genesis.at("protocolConsts").at("protocolMagic")) },
        byron_start_time { json::value_to<uint64_t>(byron_genesis.at("startTime")) },
        byron_epoch_length { 21600 },
        byron_slot_duration { std::stoull(json::value_to<std::string>(byron_genesis.at("blockVersionData").as_object().at("slotDuration"))) / 1000 },
        byron_utxos { _byron_prep_utxos(byron_genesis) },
        byron_issuers { _byron_prep_heavy(byron_genesis, "issuerPk") },
        byron_delegate_hashes { _byron_prep_hashes(_byron_prep_heavy(byron_genesis, "delegatePk")) },
        shelley_genesis { cfg.at(std::filesystem::path { json::value_to<std::string>(cfg.at("config").at("ShelleyGenesisFile")) }.stem().string()) },
        shelley_genesis_hash { _verify_hash(cfg.at("config").at("ShelleyGenesisHash").as_string(), shelley_genesis) },
        shelley_epoch_length { json::value_to<uint64_t>(shelley_genesis.at("epochLength")) },
        shelley_update_quorum { json::value_to<uint64_t>(shelley_genesis.at("updateQuorum")) },
        shelley_max_lovelace_supply { json::value_to<uint64_t>(shelley_genesis.at("maxLovelaceSupply")) },
        shelley_network_id { shelley_genesis.at("networkId").as_string() == "Mainnet" ? uint8_t { 1 } : uint8_t { 0 } },
        shelley_active_slots { json::value_to<double>(shelley_genesis.at("activeSlotsCoeff")) },
        shelley_security_param { json::value_to<uint64_t>(shelley_genesis.at("securityParam")) },
        shelley_epoch_blocks { static_cast<uint64_t>(shelley_active_slots * shelley_epoch_length) },
        shelley_rewards_ready_slot { shelley_epoch_length - static_cast<uint64_t>(std::ceil(2 * shelley_security_param / shelley_active_slots)) },
        shelley_stability_window { static_cast<uint64_t>(std::ceil(3 * shelley_security_param / shelley_active_slots)) },
        shelley_randomness_stabilization_window { static_cast<uint64_t>(std::ceil(4 * shelley_security_param / shelley_active_slots)) },
        shelley_voting_deadline { static_cast<uint64_t>(std::ceil(4 * shelley_security_param / shelley_active_slots)) },
        shelley_delegates { _shelley_prep_delegates(shelley_genesis) },
        alonzo_genesis { cfg.at(std::filesystem::path { json::value_to<std::string>(cfg.at("config").at("AlonzoGenesisFile")) }.stem().string()) },
        alonzo_genesis_hash { _verify_hash(cfg.at("config").at("AlonzoGenesisHash").as_string(), alonzo_genesis) },
        conway_genesis { cfg.at(std::filesystem::path { json::value_to<std::string>(cfg.at("config").at("ConwayGenesisFile")) }.stem().string()) },
        conway_genesis_hash { _verify_hash(cfg.at("config").at("ConwayGenesisHash").as_string(), conway_genesis) },
        plutus_all_cost_models { _prep_plutus_cost_models(alonzo_genesis) },
        conway_pool_voting_thresholds { decltype(conway_pool_voting_thresholds)::from_json(conway_genesis.at("poolVotingThresholds").as_object()) },
        conway_drep_voting_thresholds { decltype(conway_drep_voting_thresholds)::from_json(conway_genesis.at("dRepVotingThresholds").as_object()) }
    {
        shelley_start_epoch({});
    }

    void config::shelley_start_epoch(std::optional<uint64_t> epoch) const
    {
        static const auto mainnet_hash = uint8_vector::from_hex("15a199f895e461ec0ffc6dd4e4028af28a492ab4e806d39cb674c88f7643ef62");
        if (!epoch && conway_genesis_hash == mainnet_hash)
            epoch = 208;
        if (epoch)
            _shelley_start_slot.emplace(*epoch * byron_epoch_length);
        else
            _shelley_start_slot.reset();
    }
}