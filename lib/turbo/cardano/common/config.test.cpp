/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/base64.hpp>
#include <turbo/cardano/common/config.hpp>
#include <turbo/common/test.hpp>
#include <concepts>
#include <type_traits>

using namespace turbo;
using namespace turbo::cardano;

namespace {
    configs_mock::map_type dijkstra_config_files(const std::string_view network = "dijkstra")
    {
        const configs_dir source { configs_dir::network_path(network) };
        configs_mock::map_type files;
        for (const auto name: {"config", "byron-genesis", "shelley-genesis", "alonzo-genesis", "conway-genesis", "dijkstra-genesis"})
            files.emplace(name, config_json { source.at(name) });
        return files;
    }

    void replace_dijkstra_genesis(configs_mock::map_type &files, json::object genesis)
    {
        files.at("dijkstra-genesis") = config_json { std::move(genesis) };
        auto node = files.at("config").json();
        node.at("DijkstraGenesisHash") = fmt::format("{}",
            crypto::blake2b::digest<block_hash>(files.at("dijkstra-genesis").bytes()));
        files.at("config") = config_json { std::move(node) };
    }

    void replace_shelley_genesis(configs_mock::map_type &files, json::object genesis)
    {
        files.at("shelley-genesis") = config_json { std::move(genesis) };
        auto node = files.at("config").json();
        node.at("ShelleyGenesisHash") = fmt::format("{}",
            crypto::blake2b::digest<block_hash>(files.at("shelley-genesis").bytes()));
        files.at("config") = config_json { std::move(node) };
    }
}

suite cardano_config_suite = [] {
    "cardano::config"_test = [] {
        static_assert(std::is_nothrow_move_constructible_v<cardano::config>);
        static_assert(std::is_nothrow_move_assignable_v<cardano::config>);
        static_assert(std::movable<cardano::config>);
        const cardano::config cfg {};
        "embedded and legacy Shelley genesis injections agree"_test = [] {
            auto files = dijkstra_config_files();
            auto genesis = files.at("shelley-genesis").json();
            auto &extra = genesis.at("extraConfig");
            auto &pool = extra.at("stakePools").at("data").as_object().begin()->value();
            const auto owner = extra.at("stakeCredentials").at("data").as_object().begin()->key();
            pool.at("owners") = json::array { json::string { owner } };
            pool.at("margin") = 0.25;
            pool.at("metadata") = json::object {
                { "url", "https://example.com/pool.json" }, { "hash", std::string(64, '0') }
            };
            pool.at("relays") = json::array {
                json::object { { "single host address", json::object { { "port", 3001 }, { "IPv4", "127.0.0.1" }, { "IPv6", "::1" } } } },
                json::object { { "single host name", json::object { { "dnsName", "relay.example.com" } } } },
                json::object { { "multi host name", json::object { { "dnsName", "relays.example.com" } } } }
            };
            replace_shelley_genesis(files, json::object { genesis });
            const cardano::config embedded { configs_mock { configs_mock::map_type { files } } };
            const auto &params = embedded.shelley_initial_state.pools.begin()->second;
            expect(params.margin == rational_u64 { 1, 4 });
            expect_equal(params.owners.size(), 1ULL);
            expect_equal(params.metadata->hash.size(), 32ULL);
            expect_equal(params.relays.size(), 3ULL);
            const auto &relay = std::get<relay_addr>(params.relays.at(0).val);
            expect_equal(*relay.port, 3001);
            expect(*relay.ipv4 == ipv4_addr { buffer { uint8_vector::from_hex("7F000001") } });
            expect(*relay.ipv6 == ipv6_addr { buffer { uint8_vector::from_hex("00000000000000000000000000000001") } });
            genesis.at("initialFunds") = extra.at("initialFunds").at("data");
            genesis.at("staking").at("pools") = extra.at("stakePools").at("data");
            genesis.at("staking").at("stake") = extra.at("stakeCredentials").at("data");
            auto &legacy_pool = genesis.at("staking").at("pools").as_object().begin()->value().as_object();
            const auto pool_id = legacy_pool.at("poolId");
            const auto reward = legacy_pool.at("accountAddress");
            legacy_pool.erase("poolId");
            legacy_pool.erase("accountAddress");
            legacy_pool.emplace("publicKey", pool_id);
            legacy_pool.emplace("rewardAccount", reward);
            genesis.erase("extraConfig");
            replace_shelley_genesis(files, std::move(genesis));
            const cardano::config legacy { configs_mock { std::move(files) } };
            expect(legacy.shelley_initial_state.utxos == embedded.shelley_initial_state.utxos);
            expect(legacy.shelley_initial_state.pools == embedded.shelley_initial_state.pools);
            expect(legacy.shelley_initial_state.delegations == embedded.shelley_initial_state.delegations);
        };
        "invalid Shelley genesis injections fail during configuration loading"_test = [] {
            for (size_t fault = 0; fault < 5; ++fault) {
                auto files = dijkstra_config_files();
                auto genesis = files.at("shelley-genesis").json();
                auto &extra = genesis.at("extraConfig");
                switch (fault) {
                    case 0:
                        genesis.at("initialFunds") = extra.at("initialFunds").at("data");
                        break;
                    case 1:
                        extra.at("stakeCredentials").at("data").as_object().begin()->value() = std::string(56, '0');
                        break;
                    case 2:
                        extra.at("initialFunds").at("data").as_object().begin()->value() = std::numeric_limits<uint64_t>::max();
                        break;
                    case 3:
                        extra.at("initialFunds") = json::object { { "file", json::array { "funds.json" } }, { "hash", std::string(64, '0') } };
                        break;
                    case 4:
                        genesis.at("networkId") = "Mainnet";
                        break;
                }
                replace_shelley_genesis(files, std::move(genesis));
                expect(throws<error>([&] { const cardano::config invalid { configs_mock { std::move(files) } }; })) << fault;
            }
        };
        "Dijkstra genesis loads without activating Dijkstra rules"_test = [] {
            const configs_dir files { configs_dir::network_path("dijkstra") };
            const cardano::config network { files };
            expect_equal(network.byron_protocol_magic, 6U);
            expect_equal(network.initial_protocol_version.major, 11ULL);
            expect_equal(network.shelley_start_epoch(), 0ULL);
            expect(network.alonzo_protocol_params.ex_unit_prices.mem == rational_u64 { 577, 10000 });
            expect(network.alonzo_protocol_params.ex_unit_prices.steps == rational_u64 { 721, 10000000 });
            expect(network.dijkstra_genesis.has_value());
            if (!network.dijkstra_genesis)
                return;
            const auto &genesis = *network.dijkstra_genesis;
            expect(genesis.hash == block_hash::from_hex("ad58e282eff1f48c241faf6319f88ed54843821627a6be69470f05d7904e4601"));
            expect(genesis.parameters == files.at("dijkstra-genesis").json());
            expect_equal(genesis.leios_committee_size, 900ULL);
            expect_equal(genesis.max_endorser_block_execution_units.mem, 720000000ULL);
            expect_equal(genesis.max_endorser_block_execution_units.steps, 200000000000ULL);
            expect(genesis.leios_quorum_stake_threshold == rational_u64 { 3, 4 });
            expect(genesis.min_pool_margin == rational_u64 { 3, 200 });
            expect(genesis.ref_script_cost_multiplier == rational_u64 { 6, 5 });
            expect_equal(genesis.ref_script_cost_stride, 25600U);
            expect(!genesis.max_pledge_leverage.has_value());
            const auto &costs = files.at("dijkstra-genesis").at("plutusV4CostModel").as_array();
            expect_equal(genesis.plutus_v4_cost_model.size(), costs.size());
            for (size_t i = 0; i < costs.size(); ++i)
                expect_equal(genesis.plutus_v4_cost_model[i], json::value_to<int64_t>(costs[i]));
            cardano::config copy { network };
            cardano::config moved { std::move(copy) };
            expect(&moved.dijkstra_genesis == &network.dijkstra_genesis);
            expect(&copy.dijkstra_genesis == &network.dijkstra_genesis);
        };
        "Dijkstra genesis references and hashes are required together"_test = [] {
            for (const auto field: {"DijkstraGenesisFile", "DijkstraGenesisHash"}) {
                auto files = dijkstra_config_files();
                auto node = files.at("config").json();
                node.erase(field);
                files.at("config") = config_json { std::move(node) };
                expect(throws([&] { const cardano::config invalid { configs_mock { std::move(files) } }; }));
            }
            for (const auto path: {"missing.json", "other/dijkstra-genesis.json", "../dijkstra-genesis.json", ""}) {
                auto files = dijkstra_config_files();
                auto node = files.at("config").json();
                node.at("DijkstraGenesisFile") = path;
                files.at("config") = config_json { std::move(node) };
                expect(throws([&] { const cardano::config invalid { configs_mock { std::move(files) } }; }));
            }
            auto files = dijkstra_config_files();
            auto genesis = files.at("dijkstra-genesis").json();
            genesis.at("leiosCommitteeSize") = 901;
            // Keep the old advertised hash: tampering must fail before parsing.
            files.at("dijkstra-genesis") = config_json { std::move(genesis) };
            expect(throws([&] { const cardano::config invalid { configs_mock { std::move(files) } }; }));
        };
        "Dijkstra genesis parses optional leverage and rejects malformed parameters"_test = [] {
            auto files = dijkstra_config_files();
            auto genesis = files.at("dijkstra-genesis").json();
            genesis.at("maxPledgeLeverage") = 2.5;
            replace_dijkstra_genesis(files, std::move(genesis));
            const cardano::config network { configs_mock { std::move(files) } };
            expect(network.dijkstra_genesis->max_pledge_leverage == std::optional { rational_u64 { 5, 2 } });
            for (const auto field: {"leiosCommitteeSize", "plutusV4CostModel", "minPoolMargin"}) {
                auto malformed = dijkstra_config_files();
                auto data = malformed.at("dijkstra-genesis").json();
                data.at(field) = "invalid";
                replace_dijkstra_genesis(malformed, std::move(data));
                expect(throws([&] { const cardano::config invalid { configs_mock { std::move(malformed) } }; }));
            }
            for (const auto field: {"plutusV4CostModel", "refScriptCostStride", "leiosQuorumStakeThreshold"}) {
                auto malformed = dijkstra_config_files();
                auto data = malformed.at("dijkstra-genesis").json();
                if (std::string_view { field } == "plutusV4CostModel")
                    data.at(field) = json::array {};
                else if (std::string_view { field } == "refScriptCostStride")
                    data.at(field) = 0;
                else
                    data.at(field) = json::object { {"numerator", 1}, {"denominator", 0} };
                replace_dijkstra_genesis(malformed, std::move(data));
                expect(throws([&] { const cardano::config invalid { configs_mock { std::move(malformed) } }; }));
            }
        };
        "Dijkstra genesis enforces parameter widths and nullable upgrade fields"_test = [] {
            for (const auto field: { "leiosAnnouncementPeriodLength", "leiosVotePeriodLength",
                    "leiosDiffusionPeriodLength", "maxEndorserBlockReferencesSize", "maxEndorserBlockTxsSize",
                    "perasMinCandidateBlockAge", "perasBootstrapRound" }) {
                auto files = dijkstra_config_files();
                auto genesis = files.at("dijkstra-genesis").json();
                genesis.at(field) = uint64_t { 1 } << 32;
                replace_dijkstra_genesis(files, std::move(genesis));
                expect(throws([&] { const cardano::config invalid { configs_mock { std::move(files) } }; }));
            }
            for (const auto field: { "leiosCommitteeSize", "perasCertBoost", "perasTargetCommitteeSize" }) {
                auto files = dijkstra_config_files();
                auto genesis = files.at("dijkstra-genesis").json();
                genesis.at(field) = 65536;
                replace_dijkstra_genesis(files, std::move(genesis));
                expect(throws([&] { const cardano::config invalid { configs_mock { std::move(files) } }; }));
            }
            for (const auto field: { "refScriptCostMultiplier", "perasHealingFactor" }) {
                auto files = dijkstra_config_files();
                auto genesis = files.at("dijkstra-genesis").json();
                genesis.at(field) = 0;
                replace_dijkstra_genesis(files, std::move(genesis));
                expect(throws([&] { const cardano::config invalid { configs_mock { std::move(files) } }; }));
            }
            auto files = dijkstra_config_files();
            auto genesis = files.at("dijkstra-genesis").json();
            genesis.erase("maxPledgeLeverage");
            genesis.at("perasBootstrapRound") = nullptr;
            replace_dijkstra_genesis(files, std::move(genesis));
            const cardano::config cfg { configs_mock { std::move(files) } };
            expect(!cfg.dijkstra_genesis->max_pledge_leverage);
            expect(!cfg.dijkstra_genesis->peras_bootstrap_round);
        };
        "epoch-zero Dijkstra requires its genesis"_test = [] {
            auto files = dijkstra_config_files("musashi");
            auto node = files.at("config").json();
            node.erase("DijkstraGenesisFile");
            node.erase("DijkstraGenesisHash");
            files.at("config") = config_json { std::move(node) };
            expect(throws([&] { const cardano::config invalid { configs_mock { std::move(files) } }; }));
        };
        "Dijkstra genesis loading follows configured filenames and bytes"_test = [] {
            auto files = dijkstra_config_files();
            const cardano::config original { configs_mock { dijkstra_config_files() } };
            auto node = files.at("config").json();
            node.at("DijkstraGenesisFile") = "./renamed.json";
            node["TraceOptions"] = json::object {};
            files.at("config") = config_json { std::move(node) };
            files.emplace("renamed", config_json { files.at("dijkstra-genesis") });
            const cardano::config renamed { configs_mock { std::move(files) } };
            expect(renamed.dijkstra_genesis->hash == original.dijkstra_genesis->hash);
            expect(renamed.dijkstra_genesis->parameters == original.dijkstra_genesis->parameters);
            auto changed = dijkstra_config_files();
            auto genesis = changed.at("dijkstra-genesis").json();
            genesis.at("leiosCommitteeSize") = 901;
            replace_dijkstra_genesis(changed, std::move(genesis));
            const cardano::config respun { configs_mock { std::move(changed) } };
            expect_equal(respun.byron_protocol_magic, original.byron_protocol_magic);
            expect(respun.byron_genesis_hash == original.byron_genesis_hash);
            expect(respun.dijkstra_genesis->hash != original.dijkstra_genesis->hash);
            expect_equal(respun.dijkstra_genesis->leios_committee_size, 901ULL);
        };
        "existing networks load without Dijkstra genesis"_test = [] {
            for (const auto name: {"mainnet", "preview", "preprod", "sanchonet"}) {
                const cardano::config network { configs_dir { configs_dir::network_path(name) } };
                expect(!network.dijkstra_genesis.has_value());
            }
        };
        "public testnet genesis"_test = [] {
            for (const auto name: {"preview", "preprod"}) {
                const bool preview = std::string_view{name} == "preview";
                const configs_dir files{configs_dir::network_path(name)};
                const cardano::config network{files};
                expect_equal(network.byron_protocol_magic, preview ? 2U : 1U);
                expect_equal(network.shelley_network_id, 0);
                expect_equal(network.byron_epoch_length, preview ? 4320ULL : 21600ULL);
                expect_equal(network.byron_slots_per_chunk, network.byron_epoch_length);
                expect_equal(network.shelley_epoch_length, preview ? 86400ULL : 432000ULL);
                expect_equal(network.shelley_security_param, preview ? 432ULL : 2160ULL);
                expect_equal(network.initial_protocol_version.major, preview ? 6ULL : 0ULL);
                expect_equal(network.shelley_start_known(), preview);
                const auto start = std::chrono::system_clock::time_point{
                    std::chrono::seconds{network.byron_start_time + (preview ? 86400 : 20)}
                };
                const auto slot = cardano::slot::from_time(start, network);
                expect_equal(static_cast<uint64_t>(slot), preview ? 86400ULL : 1ULL);
                expect_equal(slot.unixtime(), network.byron_start_time + (preview ? 86400 : 20));
                expect_equal(network.byron_utxos.size(), 8);
                uint64_t balance = 0;
                for (const auto &[id, output]: network.byron_utxos)
                    balance += output.coin;
                expect_equal(balance, 30'000'000'000'000'000ULL);
            }
        };
        "Shelley boundary overrides and reset"_test = [] {
            for (const auto name: {"mainnet", "preview", "preprod"}) {
                const configs_dir files{configs_dir::network_path(name)};
                const cardano::config network{files};
                const bool known = std::string_view{name} != "preprod";
                const uint64_t default_epoch = std::string_view{name} == "mainnet" ? 208 : 0;
                const auto check_default = [&] {
                    expect_equal(network.shelley_start_known(), known);
                    if (known) {
                        expect_equal(network.shelley_start_epoch(), default_epoch);
                    } else {
                        expect(throws<error>([&] { network.shelley_start_slot(); }));
                        expect(throws<error>([&] { network.shelley_start_epoch(); }));
                        expect(throws<error>([&] { network.shelley_start_time(); }));
                        const auto epoch = std::numeric_limits<uint64_t>::max() / network.byron_epoch_length;
                        expect_equal(static_cast<uint64_t>(slot::from_epoch(epoch, network)), epoch * network.byron_epoch_length);
                        const slot byron{network.byron_epoch_length + 1, network};
                        expect_equal(byron.epoch(), 1);
                        expect_equal(byron.epoch_slot(), 1);
                        expect_equal(byron.unixtime(), network.byron_start_time + (network.byron_epoch_length + 1) * network.byron_slot_duration);
                    }
                };
                check_default();
                for (const uint64_t epoch: {0ULL, 123ULL}) {
                    network.set_shelley_start_epoch(epoch);
                    expect(network.shelley_start_known());
                    expect_equal(network.shelley_start_epoch(), epoch);
                    const slot boundary{epoch * network.byron_epoch_length, network};
                    expect_equal(boundary.epoch(), epoch);
                    expect_equal(boundary.epoch_slot(), 0);
                    expect_equal(boundary.unixtime(), network.byron_start_time + boundary * network.byron_slot_duration);
                    network.reset_shelley_start_epoch();
                    check_default();
                }
            }
        };
        "Shelley boundary overflow"_test = [&] {
            const cardano::config network{cfg};
            const auto max_epoch = (std::numeric_limits<uint64_t>::max() - network.byron_start_time)
                / network.byron_slot_duration / network.byron_epoch_length;
            network.set_shelley_start_epoch(max_epoch);
            expect_equal(network.shelley_start_epoch(), max_epoch);
            expect_equal(network.shelley_start_time(), network.byron_start_time + max_epoch * network.byron_epoch_length * network.byron_slot_duration);
            expect(throws<error>([&] { network.set_shelley_start_epoch(max_epoch + 1); }));
            expect(throws<error>([&] {
                network.set_shelley_start_epoch(std::numeric_limits<uint64_t>::max() / network.byron_epoch_length + 1);
            }));
            expect_equal(network.shelley_start_epoch(), max_epoch);
        };
        "Shelley boundary defaults require both mainnet genesis hashes"_test = [] {
            const configs_dir mainnet{configs_dir::network_path("mainnet")};
            for (const auto &[era, field]: {std::pair{"Byron", "startTime"}, {"Shelley", "updateQuorum"}, {"Conway", "committeeMinSize"}}) {
                auto node = mainnet.at("config").json();
                const auto name = std::filesystem::path{json::value_to<std::string>(node.at(fmt::format("{}GenesisFile", era)))}.stem().string();
                auto genesis = mainnet.at(name).json();
                genesis.at(field) = json::value_to<uint64_t>(genesis.at(field)) + 1;
                configs_mock::map_type files;
                files.emplace(name, config_json{std::move(genesis)});
                const auto &modified = files.at(name);
                const auto hash = std::string_view{era} == "Byron"
                    ? crypto::blake2b::digest<block_hash>(json::serialize_canon(modified.json()))
                    : crypto::blake2b::digest<block_hash>(modified.bytes());
                node.at(fmt::format("{}GenesisHash", era)) = fmt::format("{}", hash);
                files.emplace("config", config_json{std::move(node)});
                for (const auto file: {"byron-genesis", "shelley-genesis", "alonzo-genesis", "conway-genesis"})
                    files.try_emplace(file, mainnet.at(file));
                const cardano::config network{configs_mock{std::move(files)}};
                const bool known = std::string_view{era} == "Conway";
                expect_equal(network.shelley_start_known(), known);
                if (known)
                    expect_equal(network.shelley_start_epoch(), 208);
            }
        };
        "mainnet genesis hashes"_test = [&] {
            expect(cfg.byron_genesis_hash == block_hash::from_hex("5f20df933584822601f9e3f8c024eb5eb252fe8cefb24d1317dc3d432e940ebb"));
            expect(cfg.shelley_genesis_hash == block_hash::from_hex("1a3be38bcbb7911969283716ad7aa550250226b76a61fc51cc9a9a35d9276d81"));
            expect(cfg.alonzo_genesis_hash == block_hash::from_hex("7e94a15f55d1e82d10f09203fa1d40f8eede58fd8066542cf6566008068ed874"));
            expect(cfg.conway_genesis_hash == block_hash::from_hex("15a199f895e461ec0ffc6dd4e4028af28a492ab4e806d39cb674c88f7643ef62"));
        };
        "mainnet utxo balances"_test = [&] {
            const auto &utxos = cfg.byron_utxos;
            expect_equal(14505, utxos.size());
            expect(utxos.contains(tx_out_ref { tx_hash::from_hex("000F09675EF28CD358039CDAD71CF1FBDB35BE5B80EED98E296B6DC8F0718D7D"), 0 }));
            expect(utxos.contains(tx_out_ref { tx_hash::from_hex("62F199A14F352004239E1F96302C15ABE38554D53E8F0088508AC9CDE9C10EC9"), 0 }));
            expect(utxos.contains(tx_out_ref { tx_hash::from_hex("8E624C6A36DC1C84B8CD85D12585EF0DA565FA7166CB5F41BE9C5F183B07F95A"), 0 }));
            expect(utxos.contains(tx_out_ref { tx_hash::from_hex("DCE0CA2A40603CC2395CD7E348FBBF91A9D5A83E54597D1095486A2307B4CAB2"), 0 }));
        };
        "plutus cost models"_test = [&] {
            expect(cfg.plutus_all_cost_models.at(0).size() == 166);
            expect(cfg.conway_protocol_params.plutus_cost_models.at(2).size() == 251);
            expect(cfg.conway_constitution.policy_id.has_value());
        };
        "byron issuers"_test = [&] {
            static std::set<vkey> orig_issuers {
                vkey::from_hex("0BDB1F5EF3D994037593F2266255F134A564658BB2DF814B3B9CEFB96DA34FA9"),
                vkey::from_hex("1BC97A2FE02C297880CE8ECFD997FE4C1EC09EE10FEEEE9F686760166B05281D"),
                vkey::from_hex("26566E86FC6B9B177C8480E275B2B112B573F6D073F9DEEA53B8D99C4ED976B3"),
                vkey::from_hex("50733161FDAFB6C8CB6FAE0E25BDF9555105B3678EFB08F1775B9E90DE4F5C77"),
                vkey::from_hex("993A8F056D2D3E50B0AC60139F10DF8F8123D5F7C4817B40DAC2B5DD8AA94A82"),
                vkey::from_hex("9A6FA343C8C6C36DE1A3556FEB411BFDF8708D5AF88DE8626D0FC6BFA4EEBB6D"),
                vkey::from_hex("D2965C869901231798C5D02D39FCA2A79AA47C3E854921B5855C82FD14708915"),
            };
            expect_equal(orig_issuers.size(), cfg.byron_issuers.size());
            for (const auto &vk: orig_issuers)
                expect(cfg.byron_issuers.contains(vk));
        };
        "shared immutable data"_test = [&] {
            cardano::config copy { cfg };
            expect(&copy.byron_utxos == &cfg.byron_utxos);
            expect(&copy.shelley_protocol_params == &cfg.shelley_protocol_params);
            copy.set_shelley_start_epoch(123);
            expect_equal(uint64_t { 123 }, copy.shelley_start_epoch());
            expect(copy.shelley_start_epoch() != cfg.shelley_start_epoch());

            cardano::config moved { std::move(copy) };
            expect(&moved.byron_utxos == &cfg.byron_utxos);
            expect_equal(uint64_t { 123 }, moved.shelley_start_epoch());
            expect(&copy.byron_utxos == &cfg.byron_utxos);

            cardano::config assigned { cfg };
            assigned = std::move(moved);
            expect(&assigned.byron_utxos == &cfg.byron_utxos);
            expect_equal(uint64_t { 123 }, assigned.shelley_start_epoch());
        };
    };
};
