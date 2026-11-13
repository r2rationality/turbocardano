/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <boost/asio/ip/address.hpp>
#include <turbo/base58.hpp>
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
                throw error{"duplicate TXO {} in the byron genesis config", txo_id};
        }
        // Byron UTxO.fromBalances hashes the serialized address, with output index zero.
        for (const auto &[address, lovelace]: byron_genesis.at("nonAvvmBalances").as_object()) {
            auto bytes = base58::decode(address);
            const tx_out_ref id{crypto::blake2b::digest<tx_hash>(bytes), 0};
            tx_out_data output{std::move(bytes), std::stoull(json::value_to<std::string>(lovelace))};
            if (!txos.try_emplace(id, std::move(output)).second) [[unlikely]]
                throw error{"duplicate TXO {} in the byron genesis config", id};
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
            throw error{"The actual hash of genesis file does not match {}!", hash_hex};
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
            throw error{"internal error: plutus {} default costs are invalid!", version};
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

    static dijkstra_genesis_t _prep_dijkstra_genesis(const turbo::config &genesis, const block_hash &hash)
    {
        dijkstra_genesis_t p {};
        p.hash = hash;
        p.parameters = genesis.json();
        const auto rational = [&](const std::string_view name, const bool unit_interval=false) {
            const auto &value = genesis.at(name);
            if (!value.is_object()) {
                const auto number = json::value_to<double>(value);
                if (!std::isfinite(number) || number < 0
                        || number >= static_cast<double>(std::numeric_limits<uint64_t>::max())) [[unlikely]]
                    throw error{"invalid Dijkstra genesis rational: {}", name};
            }
            auto result = rational_u64::from_json(value);
            if (!result.denominator || (unit_interval && result.numerator > result.denominator)) [[unlikely]]
                throw error{"invalid Dijkstra genesis rational: {}", name};
            result.normalize();
            return result;
        };
        p.leios_announcement_period_length = json::value_to<uint32_t>(genesis.at("leiosAnnouncementPeriodLength"));
        p.leios_committee_size = json::value_to<uint16_t>(genesis.at("leiosCommitteeSize"));
        p.leios_diffusion_period_length = json::value_to<uint32_t>(genesis.at("leiosDiffusionPeriodLength"));
        p.leios_quorum_stake_threshold = rational("leiosQuorumStakeThreshold", true);
        p.leios_vote_period_length = json::value_to<uint32_t>(genesis.at("leiosVotePeriodLength"));
        const auto &units = genesis.at("maxEndorserBlockExecutionUnits");
        p.max_endorser_block_execution_units = {
            json::value_to<uint64_t>(units.at("memory")), json::value_to<uint64_t>(units.at("steps"))
        };
        if (p.max_endorser_block_execution_units.mem > std::numeric_limits<int64_t>::max()
                || p.max_endorser_block_execution_units.steps > std::numeric_limits<int64_t>::max()) [[unlikely]]
            throw error{"Dijkstra genesis execution units exceed max_int64"};
        p.max_endorser_block_references_size = json::value_to<uint32_t>(genesis.at("maxEndorserBlockReferencesSize"));
        p.max_endorser_block_txs_size = json::value_to<uint32_t>(genesis.at("maxEndorserBlockTxsSize"));
        if (const auto *leverage = genesis.json().if_contains("maxPledgeLeverage"); leverage && !leverage->is_null())
            p.max_pledge_leverage = rational("maxPledgeLeverage");
        p.max_ref_script_size_per_block = json::value_to<uint32_t>(genesis.at("maxRefScriptSizePerBlock"));
        p.max_ref_script_size_per_endorser_block = json::value_to<uint32_t>(genesis.at("maxRefScriptSizePerEndorserBlock"));
        p.max_ref_script_size_per_tx = json::value_to<uint32_t>(genesis.at("maxRefScriptSizePerTx"));
        p.min_pool_margin = rational("minPoolMargin", true);
        p.ref_script_cost_stride = json::value_to<uint32_t>(genesis.at("refScriptCostStride"));
        p.ref_script_cost_multiplier = rational("refScriptCostMultiplier");
        if (!p.ref_script_cost_stride) [[unlikely]]
            throw error{"Dijkstra genesis refScriptCostStride must be positive"};
        if (!p.ref_script_cost_multiplier.numerator) [[unlikely]]
            throw error{"Dijkstra genesis refScriptCostMultiplier must be positive"};
        p.peras_min_candidate_block_age = json::value_to<uint32_t>(genesis.at("perasMinCandidateBlockAge"));
        p.peras_healing_factor = rational("perasHealingFactor");
        if (!p.peras_healing_factor.numerator) [[unlikely]]
            throw error{"Dijkstra genesis perasHealingFactor must be positive"};
        p.peras_cert_boost = json::value_to<uint16_t>(genesis.at("perasCertBoost"));
        p.peras_target_committee_size = json::value_to<uint16_t>(genesis.at("perasTargetCommitteeSize"));
        if (!genesis.at("perasBootstrapRound").is_null())
            p.peras_bootstrap_round.emplace(json::value_to<uint32_t>(genesis.at("perasBootstrapRound")));
        p.peras_quorum_threshold_safety_margin = rational("perasQuorumThresholdSafetyMargin", true);
        p.ref_inputs_cost_per_multi_asset_policy = json::value_to<uint64_t>(genesis.at("refInputsCostPerMultiAssetPolicy"));
        p.ref_inputs_cost_per_datum_byte = json::value_to<uint64_t>(genesis.at("refInputsCostPerDatumByte"));
        for (const auto &cost: genesis.at("plutusV4CostModel").as_array())
            p.plutus_v4_cost_model.emplace_back(json::value_to<int64_t>(cost));
        if (p.plutus_v4_cost_model.empty()) [[unlikely]]
            throw error{"Dijkstra genesis requires a nonempty Plutus V4 cost model"};
        return p;
    }

    static const json::object &_genesis_injection(const json::object &genesis,
        const std::string_view name, const json::object &legacy)
    {
        if (const auto *extra = genesis.if_contains("extraConfig"); extra && !extra->is_null()) {
            if (const auto *source = extra->as_object().if_contains(name); source && !source->is_null()) {
                const auto &obj = source->as_object();
                if (const auto *file = obj.if_contains("file"); file && !file->is_null()) [[unlikely]]
                    throw error{"external Shelley genesis injection files are not supported: {}", name};
                if (const auto *data = obj.if_contains("data"); data && !data->is_null()) {
                    if (!legacy.empty()) [[unlikely]]
                        throw error{"conflicting legacy and extraConfig Shelley genesis data: {}", name};
                    return data->as_object();
                }
            }
        }
        return legacy;
    }

    static uint8_t _genesis_network_id(const json::value &value)
    {
        const auto &name = value.as_string();
        if (name == "Mainnet")
            return 1;
        if (name == "Testnet")
            return 0;
        throw error{"unsupported genesis network id: {}", std::string_view { name }};
    }

    static relay_info _genesis_pool_relay(const json::value &value)
    {
        const auto &obj = value.as_object();
        const auto port = [](const json::object &fields) -> nil_optional_t<uint16_t> {
            if (const auto *v = fields.if_contains("port"); v && !v->is_null())
                return json::value_to<uint16_t>(*v);
            return {};
        };
        if (const auto *addr = obj.if_contains("single host address")) {
            relay_addr relay { .port=port(addr->as_object()) };
            if (const auto *v = addr->as_object().if_contains("IPv4"); v && !v->is_null()) {
                const auto bytes = boost::asio::ip::make_address_v4(json::value_to<std::string>(*v)).to_bytes();
                relay.ipv4.emplace(buffer { bytes.data(), bytes.size() });
            }
            if (const auto *v = addr->as_object().if_contains("IPv6"); v && !v->is_null()) {
                const auto bytes = boost::asio::ip::make_address_v6(json::value_to<std::string>(*v)).to_bytes();
                relay.ipv6.emplace(buffer { bytes.data(), bytes.size() });
            }
            return { std::move(relay) };
        }
        if (const auto *host = obj.if_contains("single host name"))
            return { relay_host { port(host->as_object()), json::value_to<std::string>(host->at("dnsName")) } };
        return { relay_dns { json::value_to<std::string>(obj.at("multi host name").at("dnsName")) } };
    }

    static shelley_initial_state_t _prep_shelley_initial_state(const turbo::config &genesis)
    {
        shelley_initial_state_t result {};
        static const json::object empty {};
        const auto *staking = genesis.json().if_contains("staking");
        const auto &funds = _genesis_injection(genesis.json(), "initialFunds", genesis.at("initialFunds").as_object());
        const auto &pools = _genesis_injection(genesis.json(), "stakePools", staking ? staking->at("pools").as_object() : empty);
        const auto &delegations = _genesis_injection(genesis.json(), "stakeCredentials", staking ? staking->at("stake").as_object() : empty);
        if ((!funds.empty() || !pools.empty() || !delegations.empty()) && _genesis_network_id(genesis.at("networkId"))) [[unlikely]]
            throw error{"Shelley genesis funds and staking injection is only supported on testnets"};
        auto remaining = json::value_to<uint64_t>(genesis.at("maxLovelaceSupply"));
        for (const auto &[addr, value]: funds) {
            auto bytes = uint8_vector::from_hex(addr);
            static_cast<void>(address { bytes });
            const auto coin = json::value_to<uint64_t>(value);
            if (coin > remaining || coin > std::numeric_limits<int64_t>::max()) [[unlikely]]
                throw error{"Shelley genesis funds exceed the available supply"};
            remaining -= coin;
            const tx_out_ref id { crypto::blake2b::digest<tx_hash>(bytes), 0 };
            if (!result.utxos.try_emplace(id, tx_out_data { std::move(bytes), coin }).second) [[unlikely]]
                throw error{"duplicate Shelley genesis output: {}", id};
        }
        result.pools.reserve(pools.size());
        for (const auto &[key, value]: pools) {
            const auto id = pool_hash::from_hex(key);
            const auto &obj = value.as_object();
            const auto *pool_id = obj.if_contains("poolId");
            if (id != pool_hash::from_hex((pool_id ? *pool_id : obj.at("publicKey")).as_string())) [[unlikely]]
                throw error{"Shelley genesis pool id differs from its map key: {}", std::string_view { key }};
            pool_params params {};
            params.vrf_vkey = vrf_vkey::from_hex(obj.at("vrf").as_string());
            params.pledge = json::value_to<uint64_t>(obj.at("pledge"));
            params.cost = json::value_to<uint64_t>(obj.at("cost"));
            params.margin = rational_u64::from_json(obj.at("margin"));
            if (!params.margin.denominator || params.margin.numerator > params.margin.denominator) [[unlikely]]
                throw error{"Shelley genesis pool margin must be in [0, 1]"};
            const auto *account = obj.if_contains("accountAddress");
            const auto &reward = account ? *account : obj.at("rewardAccount");
            const auto &credential = reward.at("credential").as_object();
            const auto *key_hash = credential.if_contains("keyHash");
            const auto hash = stake_keyhash_t::from_hex<stake_keyhash_t>((key_hash ? *key_hash : credential.at("scriptHash")).as_string());
            params.reward_id.at(0) = (key_hash ? 0xE0 : 0xF0) | _genesis_network_id(reward.at("network"));
            std::copy(hash.begin(), hash.end(), params.reward_id.begin() + 1);
            for (const auto &owner: obj.at("owners").as_array())
                params.owners.emplace(stake_keyhash_t::from_hex<stake_keyhash_t>(owner.as_string()));
            for (const auto &relay: obj.at("relays").as_array())
                params.relays.emplace_back(_genesis_pool_relay(relay));
            if (const auto &metadata = obj.at("metadata"); !metadata.is_null()) {
                params.metadata.emplace();
                params.metadata->url = json::value_to<std::string>(metadata.at("url"));
                const auto hash_bytes = uint8_vector::from_hex(metadata.at("hash").as_string());
                params.metadata->hash = buffer { hash_bytes };
            }
            if (const auto *bls = obj.if_contains("blsKey"); bls && !bls->is_null()) [[unlikely]]
                throw error{"Shelley genesis BLS pool keys are not supported"};
            result.pools.try_emplace(id, std::move(params));
        }
        result.delegations.reserve(delegations.size());
        for (const auto &[key, value]: delegations) {
            const auto pool = pool_hash::from_hex(value.as_string());
            if (!result.pools.contains(pool)) [[unlikely]]
                throw error{"Shelley genesis delegates to an unknown pool: {}", pool};
            result.delegations.try_emplace(stake_keyhash_t::from_hex<stake_keyhash_t>(key), pool);
        }
        return result;
    }

    struct config::immutable {
        block_hash byron_genesis_hash {};
        uint32_t byron_protocol_magic = 0;
        uint64_t byron_start_time = 0;
        uint64_t byron_epoch_length = 0;
        uint64_t byron_slot_duration = 0;
        txo_map byron_utxos {};
        byron_delegate_map byron_delegates {};
        vkey_set byron_issuers {};
        signer_set byron_delegate_hashes {};
        uint64_t byron_slots_per_chunk = 0;
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
        shelley_initial_state_t shelley_initial_state {};
        block_hash alonzo_genesis_hash {};
        block_hash conway_genesis_hash {};
        std::optional<dijkstra_genesis_t> dijkstra_genesis {};
        plutus_cost_models plutus_all_cost_models {};
        pool_voting_thresholds_t conway_pool_voting_thresholds {};
        drep_voting_thresholds_t conway_drep_voting_thresholds {};
        protocol_params shelley_protocol_params {};
        protocol_params alonzo_protocol_params {};
        protocol_params conway_protocol_params {};
        protocol_version initial_protocol_version{0, 0};
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
            byron_epoch_length = 10 * json::value_to<uint64_t>(byron.at("protocolConsts").at("k"));
            if (!byron_epoch_length) [[unlikely]]
                throw error{"Byron epoch length must be positive"};
            byron_slots_per_chunk = byron_epoch_length;
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
            if (json::value_to<uint32_t>(shelley.at("networkMagic")) != byron_protocol_magic) [[unlikely]]
                throw error{"Byron and Shelley network magic differ"};
            {
                static constexpr std::array<std::string_view, 7> forks{"Shelley", "Allegra", "Mary", "Alonzo", "Babbage", "Conway", "Dijkstra"};
                bool previous_forced = true;
                uint64_t initial_era = 1;
                for (const auto name: forks) {
                    const auto *epoch = node_config.json().if_contains(fmt::format("Test{}HardForkAtEpoch", name));
                    if (epoch) {
                        if (json::value_to<uint64_t>(*epoch) != 0 || !previous_forced) [[unlikely]]
                            throw error{"only consecutive epoch-zero hard-fork overrides are supported"};
                        ++initial_era;
                    }
                    previous_forced = epoch != nullptr;
                }
                if (initial_era > 1) {
                    const auto &pv = shelley.at("protocolParams").at("protocolVersion");
                    initial_protocol_version = {json::value_to<uint64_t>(pv.at("major")), json::value_to<uint64_t>(pv.at("minor"))};
                    if (initial_protocol_version.era() != initial_era) [[unlikely]]
                        throw error{"Shelley genesis protocol version does not match the epoch-zero hard forks"};
                }
            }
            shelley_epoch_length = json::value_to<uint64_t>(shelley.at("epochLength"));
            shelley_update_quorum = json::value_to<uint64_t>(shelley.at("updateQuorum"));
            shelley_max_lovelace_supply = json::value_to<uint64_t>(shelley.at("maxLovelaceSupply"));
            shelley_network_id = _genesis_network_id(shelley.at("networkId"));
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
            shelley_initial_state = _prep_shelley_initial_state(shelley);
            if (initial_protocol_version.major < 2 && (!shelley_initial_state.utxos.empty()
                    || !shelley_initial_state.pools.empty() || !shelley_initial_state.delegations.empty())) [[unlikely]]
                throw error{"Shelley genesis injection requires an epoch-zero Shelley or later network"};

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

            const auto *dijkstra_file = node_config.json().if_contains("DijkstraGenesisFile");
            const auto *dijkstra_hash = node_config.json().if_contains("DijkstraGenesisHash");
            if (static_cast<bool>(dijkstra_file) != static_cast<bool>(dijkstra_hash)) [[unlikely]]
                throw error("DijkstraGenesisFile and DijkstraGenesisHash must be configured together");
            if (dijkstra_file) {
                const std::filesystem::path path { json::value_to<std::string>(*dijkstra_file) };
                // configs_dir indexes adjacent JSON files by stem; do not silently
                // substitute an adjacent file for a reference to another directory.
                if (path.empty() || path.extension() != ".json" || path.is_absolute()
                        || (path.has_parent_path() && path.parent_path() != ".")) [[unlikely]]
                    throw error("DijkstraGenesisFile must name a JSON file in the configuration directory");
                const auto &dijkstra = genesis("DijkstraGenesisFile");
                dijkstra_genesis = _prep_dijkstra_genesis(dijkstra,
                    config::_verify_hash(dijkstra_hash->as_string(), dijkstra));
            }
            if (initial_protocol_version.major >= 12 && !dijkstra_genesis) [[unlikely]]
                throw error{"Dijkstra genesis is required for an epoch-zero Dijkstra network"};
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
        shelley_initial_state { _immutable->shelley_initial_state },
        alonzo_genesis_hash { _immutable->alonzo_genesis_hash },
        conway_genesis_hash { _immutable->conway_genesis_hash },
        dijkstra_genesis { _immutable->dijkstra_genesis },
        plutus_all_cost_models { _immutable->plutus_all_cost_models },
        conway_pool_voting_thresholds { _immutable->conway_pool_voting_thresholds },
        conway_drep_voting_thresholds { _immutable->conway_drep_voting_thresholds },
        shelley_protocol_params { _immutable->shelley_protocol_params },
        alonzo_protocol_params { _immutable->alonzo_protocol_params },
        conway_protocol_params { _immutable->conway_protocol_params },
        initial_protocol_version{_immutable->initial_protocol_version},
        conway_committee_members { _immutable->conway_committee_members },
        conway_committee_threshold { _immutable->conway_committee_threshold },
        conway_constitution { _immutable->conway_constitution },
        _mutable { shelley_start_slot_ }
    {
    }

    config::config(const configs &cfg):
        config { std::make_shared<immutable>(cfg), std::nullopt }
    {
        reset_shelley_start_epoch();
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

    void config::set_shelley_start_epoch(const uint64_t epoch) const
    {
        if (epoch > std::numeric_limits<uint64_t>::max() / byron_epoch_length) [[unlikely]]
            throw error{"Shelley start epoch {} overflows the slot number", epoch};
        const auto slot = epoch * byron_epoch_length;
        if (byron_slot_duration && slot > (std::numeric_limits<uint64_t>::max() - byron_start_time) / byron_slot_duration) [[unlikely]]
            throw error{"Shelley start epoch {} overflows the timestamp", epoch};
        _mutable.shelley_start_slot = slot;
    }

    void config::reset_shelley_start_epoch() const
    {
        static constexpr auto mainnet_byron_hash = block_hash::from_hex("5f20df933584822601f9e3f8c024eb5eb252fe8cefb24d1317dc3d432e940ebb");
        static constexpr auto mainnet_shelley_hash = block_hash::from_hex("1a3be38bcbb7911969283716ad7aa550250226b76a61fc51cc9a9a35d9276d81");
        if (initial_protocol_version.major >= 2)
            set_shelley_start_epoch(0);
        else if (byron_genesis_hash == mainnet_byron_hash && shelley_genesis_hash == mainnet_shelley_hash)
            set_shelley_start_epoch(208);
        else
            _mutable.shelley_start_slot.reset();
    }
}
