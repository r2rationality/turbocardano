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

suite cardano_config_suite = [] {
    "cardano::config"_test = [] {
        static_assert(std::is_nothrow_move_constructible_v<cardano::config>);
        static_assert(std::is_nothrow_move_assignable_v<cardano::config>);
        static_assert(std::movable<cardano::config>);
        const cardano::config cfg {};
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
