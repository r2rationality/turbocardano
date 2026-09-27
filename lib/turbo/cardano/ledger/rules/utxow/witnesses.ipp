/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Utxow.
// Included at namespace scope by lib/turbo/cardano/common/common.cpp.

    wit_cnt tx_base::witnesses_ok_vkey(signer_set &valid_vkeys) const
    {
        valid_vkeys.reserve(valid_vkeys.size() + witnesses().size());
        const auto &tx_hash = hash();
        wit_cnt cnts {};
        foreach_witness([&](const auto &w) {
            std::visit([&](const auto &wv) {
                using T = std::decay_t<decltype(wv)>;
                if constexpr (std::is_same_v<T, tx_wit_byron_vkey>) {
                    const auto pm = block().header().protocol_magic_raw();
                    uint8_vector msg {};
                    msg.reserve(64);
                    msg << 0x01; // signing tag
                    msg << pm;   // protocol magic
                    msg << 0x58; // CBOR bytestring
                    msg << 0x20; // hash size
                    msg << tx_hash;
                    const auto vk_short = static_cast<buffer>(wv.vkey).subbuf(0, 32);
                    if (!crypto::ed25519::verify(wv.sig, vk_short, msg)) [[unlikely]]
                        throw error(fmt::format("byron tx witness type 0 failed for tx {}", tx_hash));
                    valid_vkeys.emplace(crypto::blake2b::digest<key_hash>(vk_short));
                    ++cnts.vkey;
                } else if constexpr (std::is_same_v<T, tx_wit_byron_redeemer>) {
                    const auto pm = block().header().protocol_magic_raw();
                    uint8_vector msg {};
                    msg.reserve(64);
                    msg << 0x02; // signing tag
                    msg << pm;   // protocol magic
                    msg << 0x58; // CBOR bytestring
                    msg << 0x20; // hash size
                    msg << tx_hash;
                    if (!crypto::ed25519::verify(wv.sig, wv.vkey, msg)) [[unlikely]]
                        throw error(fmt::format("byron tx witness type 2 failed for tx {}", tx_hash));
                    valid_vkeys.emplace(crypto::blake2b::digest<key_hash>(wv.vkey));
                    ++cnts.vkey;
                } else if constexpr (std::is_same_v<T, tx_wit_shelley_vkey>) {
                    if (!crypto::ed25519::verify(wv.sig, wv.vkey, hash())) [[unlikely]]
                        throw error(fmt::format("shelley vkey witness failed at slot {}: vkey: {}, sig: {} tx_hash: {}", block().slot(), wv.vkey, wv.sig, hash()));
                    valid_vkeys.emplace(crypto::blake2b::digest<key_hash>(wv.vkey));
                    ++cnts.vkey;
                } else if constexpr (std::is_same_v<T, tx_wit_shelley_bootstrap>) {
                    if (!crypto::ed25519::verify(wv.sig, wv.vkey, hash())) [[unlikely]]
                        throw error(fmt::format("shelley bootstrap witness failed at slot {}: vkey: {}, sig: {} tx_hash: {}", block().slot(), wv.vkey, wv.sig, hash()));
                    // This set feeds native scripts, which use addrTxWits only.
                    // Bootstrap roots enter UTXOW's key-hash union in preprocessing.
                    ++cnts.vkey;
                }
            }, w);
        });
        return cnts;
    }

    wit_cnt tx_base::witnesses_ok_native(const signer_set &vkeys) const
    {
        wit_cnt cnts {};
        foreach_script([&](const auto &si) {
            if (si.type() == script_type::native) {
                auto w_data = cbor::zero2::parse(si.script());
                const auto err = block().era() >= 7
                    ? native_script::validate(w_data.get(), native_script::validity_interval { validity_start(), validity_end() }, vkeys)
                    : native_script::validate(w_data.get(), block().slot(), vkeys);
                if (err) [[unlikely]]
                    throw error(fmt::format("native script for tx {} failed: {} script: {}", hash(), *err, w_data.get().to_string()));
                ++cnts.native_script;
            }
        });
        return cnts;
    }

    wit_cnt tx_base::witnesses_ok(const plutus::context *ctx) const
    {
        wit_cnt cnt {};
        signer_set valid_vkeys {};
        cnt += witnesses_ok_vkey(valid_vkeys);
        cnt += witnesses_ok_native(valid_vkeys);
        if (ctx)
            cnt += witnesses_ok_plutus(*ctx);
        return cnt;
    }
