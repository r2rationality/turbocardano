/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/base64.hpp>
#include <turbo/cardano/common/config.hpp>
#include <turbo/common/test.hpp>

using namespace turbo;
using namespace turbo::cardano;

suite cardano_config_suite = [] {
    "cardano::config"_test = [] {
        const cardano::config cfg {};
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
    };
};
