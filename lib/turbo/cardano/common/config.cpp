/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/base64.hpp>
#include <turbo/cardano/common/config.hpp>
#include <turbo/cbor/encoder.hpp>
#include <turbo/plutus/costs-config.hpp>

namespace turbo::cardano {
    const config &config::get()
    {
        static config c { configs_dir::get() };
        return c;
    }

    shelley_delegate_map config::_shelley_prep_delegates(const turbo::config &shelley_genesis)
    {
        shelley_delegate_map delegs {};
        const auto &genesis_delegs = shelley_genesis.at("genDelegs").as_object();
        delegs.reserve(genesis_delegs.size());
        for (const auto &[id, meta]: genesis_delegs) {
            delegs.try_emplace(key_hash::from_hex(id), shelley_delegate {
                pool_hash::from_hex(meta.at("delegate").as_string()),
                vrf_vkey::from_hex(meta.at("vrf").as_string())
            });
        }
        return delegs;
    }

    txo_map config::_byron_prep_utxos(const turbo::config &byron_genesis)
    {
        txo_map txos {};
        for (const auto &[redeem_key, lovelace]: byron_genesis.at("avvmDistr").as_object()) {
            const auto txo_addr = byron_avvm_addr(redeem_key);
            tx_out_ref txo_id { crypto::blake2b::digest<tx_hash>(txo_addr), 0 };
            tx_out_data txo_data { std::move(txo_addr), std::stoull(json::value_to<std::string>(lovelace)) };
            if (const auto [it, created] = txos.try_emplace(txo_id, std::move(txo_data)); !created) [[unlikely]]
                throw error(fmt::format("duplicate TXO {} in the byron genesis config", txo_id));
        }
        return txos;
    }

    byron_delegate_map config::_byron_prep_delegates(const turbo::config &genesis)
    {
        using namespace std::literals;
        byron_delegate_map delegs {};
        const auto &heavy = genesis.at("heavyDelegation").as_object();
        delegs.reserve(heavy.size());
        for (const auto &[id, info]: heavy) {
            static_cast<void>(id);
            const auto key = [&](const char *name) {
                return crypto::ed25519::vkey_full { static_cast<buffer>(base64::decode(
                    json::value_to<std::string_view>(info.at(name)))) };
            };
            byron_delegate_info delegate {
                .issuer=key("issuerPk"),
                .delegate=key("delegatePk"),
                .certificate=crypto::ed25519::signature::from_hex(
                    json::value_to<std::string_view>(info.at("cert"))),
                .epoch=json::value_to<uint64_t>(info.at("omega"))
            };
            cbor::encoder epoch_enc {};
            epoch_enc.uint(delegate.epoch);
            delegate.epoch_cbor = std::move(epoch_enc.cbor());
            cbor::encoder magic_enc {};
            magic_enc.uint(json::value_to<uint64_t>(
                genesis.at("protocolConsts").as_object().at("protocolMagic")));
            uint8_vector payload {};
            payload << "00"sv << delegate.delegate << delegate.epoch_cbor;
            cbor::encoder payload_enc {};
            payload_enc.bytes(payload);
            uint8_vector signed_data {};
            signed_data << "\x0a"sv << magic_enc.cbor() << payload_enc.cbor();
            if (!crypto::ed25519::verify(delegate.certificate,
                    static_cast<buffer>(delegate.issuer).subspan(0, sizeof(vkey)), signed_data)) [[unlikely]]
                throw error("invalid Byron heavy-delegation certificate in genesis");
            const vkey issuer { static_cast<buffer>(delegate.issuer).subspan(0, sizeof(vkey)) };
            if (!delegs.try_emplace(issuer, std::move(delegate)).second) [[unlikely]]
                throw error("duplicate Byron heavy-delegation issuer in genesis");
        }
        return delegs;
    }

    block_hash config::_verify_hash_byron(const std::string_view &hash_hex, const turbo::config &genesis)
    {
        const auto cfg_hash = block_hash::from_hex(hash_hex);
        const auto cfg_canon = json::serialize_canon(genesis.json());
        auto act_hash = crypto::blake2b::digest<block_hash>(cfg_canon);
        if (act_hash != cfg_hash) [[unlikely]]
            throw error("The actual hash of ByronGenesisFile does not match ByronGenesisHash!");
        return act_hash;
    }

    block_hash config::_verify_hash(const std::string_view &hash_hex, const turbo::config &genesis)
    {
        const auto cfg_hash = block_hash::from_hex(hash_hex);
        auto act_hash = crypto::blake2b::digest<block_hash>(genesis.bytes());
        if (act_hash != cfg_hash) [[unlikely]]
            throw error(fmt::format("The actual hash of genesis file does not match {}!", hash_hex));
        return act_hash;
    }

    static plutus_cost_model _make_plutus_default_cost_model(const std::vector<std::string> &names, const plutus::costs::arg_map &d_args,
        const size_t exp_size, const std::string_view version)
    {
        plutus_cost_model::raw_value_type costs {};
        costs.reserve(exp_size);
        for (size_t i = 0; i < names.size() && i < exp_size; ++i) {
            const auto &name = names[i];
            costs.emplace_back(std::stoll(d_args.at(plutus::costs::canonical_arg_name(name))));
        }
        if (costs.size() != exp_size) [[unlikely]]
            throw error(fmt::format("internal error: plutus {} default costs are invalid!", version));
        return plutus_cost_model { std::move(costs), names };
    }

    static plutus_cost_model _make_plutus_v1_default_cost_model()
    {
        return _make_plutus_default_cost_model(plutus::costs::cost_arg_names_v1(), plutus::costs::default_cost_args_a(), 166, "v1");
    }

    static plutus_cost_model _make_plutus_v2_default_cost_model()
    {
        return _make_plutus_default_cost_model(plutus::costs::cost_arg_names_v2(), plutus::costs::default_cost_args_b(), 175, "v2");
    }

    static plutus_cost_model _make_plutus_v3_default_cost_model()
    {
        return _make_plutus_default_cost_model(plutus::costs::cost_arg_names_v3(), plutus::costs::default_cost_args_c(), 251, "v3");
    }

    plutus_cost_models config::_prep_plutus_cost_models(const turbo::config &genesis)
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
        res.items.emplace(0, import("PlutusV1", v1_defaults));
        res.items.emplace(1, import("PlutusV2", v2_defaults));
        res.items.emplace(2, import("PlutusV3", v3_defaults));
        return res;
    }

    static protocol_params _prep_shelley_protocol_params(const turbo::config &genesis)
    {
        const auto &params = genesis.at("protocolParams").as_object();
        protocol_params p {};
        p.min_fee_a = json::value_to<uint64_t>(params.at("minFeeA"));
        p.min_fee_b = json::value_to<uint64_t>(params.at("minFeeB"));
        p.max_block_body_size = json::value_to<uint64_t>(params.at("maxBlockBodySize"));
        p.max_transaction_size = json::value_to<uint64_t>(params.at("maxTxSize"));
        p.max_block_header_size = json::value_to<uint64_t>(params.at("maxBlockHeaderSize"));
        p.key_deposit = json::value_to<uint64_t>(params.at("keyDeposit"));
        p.pool_deposit = json::value_to<uint64_t>(params.at("poolDeposit"));
        p.e_max = json::value_to<uint64_t>(params.at("eMax"));
        p.n_opt = json::value_to<uint64_t>(params.at("nOpt"));
        p.expansion_rate = rational_u64::from_double(json::value_to<double>(params.at("rho")));
        p.treasury_growth_rate = rational_u64::from_double(json::value_to<double>(params.at("tau")));
        p.pool_pledge_influence = rational_u64::from_double(json::value_to<double>(params.at("a0")));
        p.decentralization = rational_u64::from_double(json::value_to<double>(params.at("decentralisationParam")));
        p.min_utxo_value = json::value_to<uint64_t>(params.at("minUTxOValue"));
        p.min_pool_cost = json::value_to<uint64_t>(params.at("minPoolCost"));
        return p;
    }

    static protocol_params _prep_alonzo_protocol_params(
            const turbo::config &genesis,
            const plutus_cost_models &all_cost_models)
    {
        protocol_params p {};
        p.lovelace_per_utxo_byte = json::value_to<uint64_t>(genesis.at("lovelacePerUTxOWord"));
        p.ex_unit_prices = decltype(p.ex_unit_prices)::from_json(genesis.at("executionPrices"));
        p.max_tx_ex_units = decltype(p.max_tx_ex_units)::from_json(genesis.at("maxTxExUnits"));
        p.max_block_ex_units = decltype(p.max_block_ex_units)::from_json(genesis.at("maxBlockExUnits"));
        p.max_value_size = json::value_to<uint64_t>(genesis.at("maxValueSize"));
        p.max_collateral_pct = json::value_to<uint64_t>(genesis.at("collateralPercentage"));
        p.max_collateral_inputs = json::value_to<uint64_t>(genesis.at("maxCollateralInputs"));
        p.plutus_cost_models.items.emplace(0, plutus_cost_model::from_json(
            all_cost_models.at(0), genesis.at("costModels").at("PlutusV1")));
        return p;
    }

    static protocol_params _prep_conway_protocol_params(
            const turbo::config &genesis,
            const plutus_cost_models &all_cost_models)
    {
        protocol_params p {};
        const auto &v3_json = genesis.at("plutusV3CostModel");
        const auto &v3_defaults = all_cost_models.at(2);
        if (v3_json.is_array() && v3_json.as_array().size() < v3_defaults.raw_values().size()) {
            // Older Conway genesis files (notably SanchoNet) contain the original
            // 233-entry Plutus V3 model. Cost-model arrays are ordered prefixes, so
            // retain that historical size instead of manufacturing values for
            // parameters introduced by later protocol versions.
            auto legacy_raw = v3_defaults.raw_values();
            legacy_raw.resize(v3_json.as_array().size());
            const plutus_cost_model legacy_defaults {
                std::move(legacy_raw), plutus::costs::cost_arg_names_v3()
            };
            p.plutus_cost_models.items.emplace(2,
                plutus_cost_model::from_json(legacy_defaults, v3_json));
        } else {
            p.plutus_cost_models.items.emplace(2,
                plutus_cost_model::from_json(v3_defaults, v3_json));
        }
        p.pool_voting_thresholds = decltype(p.pool_voting_thresholds)::from_json(
            genesis.at("poolVotingThresholds").as_object());
        p.drep_voting_thresholds = decltype(p.drep_voting_thresholds)::from_json(
            genesis.at("dRepVotingThresholds").as_object());
        p.committee_min_size = json::value_to<uint64_t>(genesis.at("committeeMinSize"));
        p.committee_max_term_length = json::value_to<uint64_t>(genesis.at("committeeMaxTermLength"));
        p.gov_action_lifetime = json::value_to<uint64_t>(genesis.at("govActionLifetime"));
        p.gov_action_deposit = json::value_to<uint64_t>(genesis.at("govActionDeposit"));
        p.drep_deposit = json::value_to<uint64_t>(genesis.at("dRepDeposit"));
        p.drep_activity = json::value_to<uint64_t>(genesis.at("dRepActivity"));
        p.min_fee_ref_script_cost_per_byte = decltype(p.min_fee_ref_script_cost_per_byte)::from_json(
            json::value_to<double>(genesis.at("minFeeRefScriptCostPerByte")));
        return p;
    }

    struct config::immutable {
        block_hash byron_genesis_hash {};
        uint32_t byron_protocol_magic = 0;
        uint64_t byron_start_time = 0;
        uint64_t byron_epoch_length = 21600;
        uint64_t byron_slot_duration = 0;
        txo_map byron_utxos {};
        byron_delegate_map byron_delegates {};
        vkey_set byron_issuers {};
        signer_set byron_delegate_hashes {};
        uint64_t byron_slots_per_chunk = 21600;
        block_hash shelley_genesis_hash {};
        uint64_t shelley_epoch_length = 0;
        uint64_t shelley_update_quorum = 0;
        uint64_t shelley_max_lovelace_supply = 0;
        uint8_t shelley_network_id = 0;
        double shelley_active_slots = 0;
        rational_u64 shelley_active_slots_coeff {};
        uint64_t shelley_slots_per_kes_period = 0;
        uint64_t shelley_max_kes_evolutions = 0;
        uint64_t shelley_security_param = 0;
        uint64_t shelley_epoch_blocks = 0;
        uint64_t shelley_rewards_ready_slot = 0;
        uint64_t shelley_stability_window = 0;
        uint64_t shelley_randomness_stabilization_window = 0;
        uint64_t shelley_voting_deadline = 0;
        uint64_t shelley_chunks_per_epoch = 0;
        shelley_delegate_map shelley_delegates {};
        block_hash alonzo_genesis_hash {};
        block_hash conway_genesis_hash {};
        plutus_cost_models plutus_all_cost_models {};
        pool_voting_thresholds_t conway_pool_voting_thresholds {};
        drep_voting_thresholds_t conway_drep_voting_thresholds {};
        protocol_params shelley_protocol_params {};
        protocol_params alonzo_protocol_params {};
        protocol_params conway_protocol_params {};
        committee_member_map conway_committee_members {};
        rational_u64 conway_committee_threshold {};
        constitution_t conway_constitution {};

        explicit immutable(const configs &cfg)
        {
            const auto &node_config = cfg.at("config");
            const auto genesis = [&](const std::string_view name) -> const turbo::config & {
                return cfg.at(std::filesystem::path {
                    json::value_to<std::string>(node_config.at(name))
                }.stem().string());
            };

            const auto &byron = genesis("ByronGenesisFile");
            byron_genesis_hash = config::_verify_hash_byron(
                node_config.at("ByronGenesisHash").as_string(), byron);
            byron_protocol_magic = json::value_to<uint32_t>(byron.at("protocolConsts").at("protocolMagic"));
            byron_start_time = json::value_to<uint64_t>(byron.at("startTime"));
            byron_slot_duration = std::stoull(
                json::value_to<std::string>(byron.at("blockVersionData").as_object().at("slotDuration"))) / 1000;
            byron_utxos = config::_byron_prep_utxos(byron);
            byron_delegates = config::_byron_prep_delegates(byron);
            for (const auto &[issuer, info]: byron_delegates) {
                byron_issuers.emplace(issuer);
                byron_delegate_hashes.emplace(crypto::blake2b::digest<key_hash>(
                    static_cast<buffer>(info.delegate).subspan(0, sizeof(vkey))));
            }

            const auto &shelley = genesis("ShelleyGenesisFile");
            shelley_genesis_hash = config::_verify_hash(
                node_config.at("ShelleyGenesisHash").as_string(), shelley);
            shelley_epoch_length = json::value_to<uint64_t>(shelley.at("epochLength"));
            shelley_update_quorum = json::value_to<uint64_t>(shelley.at("updateQuorum"));
            shelley_max_lovelace_supply = json::value_to<uint64_t>(shelley.at("maxLovelaceSupply"));
            shelley_network_id = shelley.at("networkId").as_string() == "Mainnet" ? uint8_t { 1 } : uint8_t { 0 };
            shelley_active_slots_coeff = rational_u64::from_json(shelley.at("activeSlotsCoeff"));
            shelley_slots_per_kes_period = json::value_to<uint64_t>(shelley.at("slotsPerKESPeriod"));
            shelley_max_kes_evolutions = json::value_to<uint64_t>(shelley.at("maxKESEvolutions"));
            if (!shelley_active_slots_coeff.numerator
                    || shelley_active_slots_coeff.numerator > shelley_active_slots_coeff.denominator) [[unlikely]]
                throw error("activeSlotsCoeff must be in (0, 1]");
            if (!shelley_slots_per_kes_period) [[unlikely]]
                throw error("slotsPerKESPeriod must be positive");
            if (!shelley_max_kes_evolutions) [[unlikely]]
                throw error("maxKESEvolutions must be positive");
            shelley_active_slots = static_cast<double>(shelley_active_slots_coeff);
            shelley_security_param = json::value_to<uint64_t>(shelley.at("securityParam"));
            shelley_epoch_blocks = static_cast<uint64_t>(shelley_active_slots * shelley_epoch_length);
            shelley_rewards_ready_slot = shelley_epoch_length
                - static_cast<uint64_t>(std::ceil(2 * shelley_security_param / shelley_active_slots));
            shelley_stability_window = static_cast<uint64_t>(
                std::ceil(3 * shelley_security_param / shelley_active_slots));
            shelley_randomness_stabilization_window = static_cast<uint64_t>(
                std::ceil(4 * shelley_security_param / shelley_active_slots));
            shelley_voting_deadline = static_cast<uint64_t>(
                std::ceil(4 * shelley_security_param / shelley_active_slots));
            shelley_chunks_per_epoch = shelley_epoch_length / byron_slots_per_chunk;
            shelley_delegates = config::_shelley_prep_delegates(shelley);
            shelley_protocol_params = _prep_shelley_protocol_params(shelley);

            const auto &alonzo = genesis("AlonzoGenesisFile");
            alonzo_genesis_hash = config::_verify_hash(
                node_config.at("AlonzoGenesisHash").as_string(), alonzo);
            plutus_all_cost_models = config::_prep_plutus_cost_models(alonzo);
            alonzo_protocol_params = _prep_alonzo_protocol_params(alonzo, plutus_all_cost_models);

            const auto &conway = genesis("ConwayGenesisFile");
            conway_genesis_hash = config::_verify_hash(
                node_config.at("ConwayGenesisHash").as_string(), conway);
            conway_protocol_params = _prep_conway_protocol_params(conway, plutus_all_cost_models);
            conway_pool_voting_thresholds = conway_protocol_params.pool_voting_thresholds;
            conway_drep_voting_thresholds = conway_protocol_params.drep_voting_thresholds;
            const auto &committee = conway.at("committee");
            const auto &committee_members = committee.at("members").as_object();
            conway_committee_members.reserve(committee_members.size());
            for (const auto &[cred, epoch]: committee_members) {
                conway_committee_members.try_emplace(
                    credential_t::from_json(cred), json::value_to<uint64_t>(epoch));
            }
            conway_committee_threshold = decltype(conway_committee_threshold)::from_json(
                committee.at("threshold"));
            conway_constitution = constitution_t::from_json(conway.at("constitution"));
        }
    };

    config::config(
            std::shared_ptr<const immutable> immutable_,
            const std::optional<uint64_t> shelley_start_slot_) noexcept:
        _immutable { std::move(immutable_) },
        byron_genesis_hash { _immutable->byron_genesis_hash },
        byron_protocol_magic { _immutable->byron_protocol_magic },
        byron_start_time { _immutable->byron_start_time },
        byron_epoch_length { _immutable->byron_epoch_length },
        byron_slot_duration { _immutable->byron_slot_duration },
        byron_utxos { _immutable->byron_utxos },
        byron_delegates { _immutable->byron_delegates },
        byron_issuers { _immutable->byron_issuers },
        byron_delegate_hashes { _immutable->byron_delegate_hashes },
        byron_slots_per_chunk { _immutable->byron_slots_per_chunk },
        shelley_genesis_hash { _immutable->shelley_genesis_hash },
        shelley_epoch_length { _immutable->shelley_epoch_length },
        shelley_update_quorum { _immutable->shelley_update_quorum },
        shelley_max_lovelace_supply { _immutable->shelley_max_lovelace_supply },
        shelley_network_id { _immutable->shelley_network_id },
        shelley_active_slots { _immutable->shelley_active_slots },
        shelley_active_slots_coeff { _immutable->shelley_active_slots_coeff },
        shelley_slots_per_kes_period { _immutable->shelley_slots_per_kes_period },
        shelley_max_kes_evolutions { _immutable->shelley_max_kes_evolutions },
        shelley_security_param { _immutable->shelley_security_param },
        shelley_epoch_blocks { _immutable->shelley_epoch_blocks },
        shelley_rewards_ready_slot { _immutable->shelley_rewards_ready_slot },
        shelley_stability_window { _immutable->shelley_stability_window },
        shelley_randomness_stabilization_window { _immutable->shelley_randomness_stabilization_window },
        shelley_voting_deadline { _immutable->shelley_voting_deadline },
        shelley_chunks_per_epoch { _immutable->shelley_chunks_per_epoch },
        shelley_delegates { _immutable->shelley_delegates },
        alonzo_genesis_hash { _immutable->alonzo_genesis_hash },
        conway_genesis_hash { _immutable->conway_genesis_hash },
        plutus_all_cost_models { _immutable->plutus_all_cost_models },
        conway_pool_voting_thresholds { _immutable->conway_pool_voting_thresholds },
        conway_drep_voting_thresholds { _immutable->conway_drep_voting_thresholds },
        shelley_protocol_params { _immutable->shelley_protocol_params },
        alonzo_protocol_params { _immutable->alonzo_protocol_params },
        conway_protocol_params { _immutable->conway_protocol_params },
        conway_committee_members { _immutable->conway_committee_members },
        conway_committee_threshold { _immutable->conway_committee_threshold },
        conway_constitution { _immutable->conway_constitution },
        _mutable { shelley_start_slot_ }
    {
    }

    config::config(const configs &cfg):
        config { std::make_shared<immutable>(cfg), std::nullopt }
    {
        shelley_start_epoch({});
    }

    config::config(const config &o):
        config { o._immutable, o._mutable.shelley_start_slot }
    {
    }

    config::config(config &&o) noexcept:
        // Keep the source's reference facade valid as well. Sharing the
        // immutable payload is still constant-time and avoids every deep copy.
        config { o._immutable, o._mutable.shelley_start_slot }
    {
    }

    config &config::operator=(config &&o) noexcept
    {
        if (this != &o) {
            this->~config();
            std::construct_at(this, std::move(o));
        }
        return *this;
    }

    void config::shelley_start_epoch(std::optional<uint64_t> epoch) const
    {
        static const auto mainnet_hash = uint8_vector::from_hex("15a199f895e461ec0ffc6dd4e4028af28a492ab4e806d39cb674c88f7643ef62");
        if (!epoch && conway_genesis_hash == mainnet_hash)
            epoch = 208;
        if (epoch)
            _mutable.shelley_start_slot.emplace(*epoch * byron_epoch_length);
        else
            _mutable.shelley_start_slot.reset();
    }
}
