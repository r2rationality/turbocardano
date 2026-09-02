/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/base64.hpp>
#include <turbo/cardano/common/types.hpp>
#include <turbo/crypto/crc32.hpp>
#include <turbo/crypto/sha3.hpp>

namespace turbo::cardano {
    key_hash byron_addr_root_hash(const size_t typ, const buffer vk, const buffer attrs_cbor)
    {
        cbor::encoder enc {};
        enc.array(3);
        enc.uint(typ);
        enc.array(2);
        enc.uint(typ);
        enc.bytes(vk);
        enc.raw_cbor(attrs_cbor);
        return crypto::blake2b::digest<key_hash>(crypto::sha3::digest(enc.cbor()));
    }

    static uint8_vector encode_redeem_root(const buffer redeem_vk)
    {
        cbor::encoder enc {};
        enc.array(3)
            .uint(2)
            .array(2).uint(2).bytes(redeem_vk)
            .map(0);
        return enc.cbor();
    }

    static key_hash address_hash(const buffer data)
    {
        return crypto::blake2b::digest<key_hash>(crypto::sha3::digest(data));
    }

    static uint8_vector encode_address(const buffer root_hash)
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
        enc.uint(crypto::crc32::digest(encoded_addr));
        return enc.cbor();
    }

    uint8_vector byron_avvm_addr(std::string_view redeem_vk_base64u)
    {
        const auto redeem_vk = base64::decode_url(redeem_vk_base64u);
        const auto encoded_root = encode_redeem_root(redeem_vk);
        const auto root_hash = address_hash(encoded_root);
        const auto encoded_addr = encode_address(root_hash);
        return byron_crc_protected(encoded_addr);
    }

    tx_hash byron_avvm_tx_hash(std::string_view redeem_vk)
    {
        return crypto::blake2b::digest<tx_hash>(byron_avvm_addr(redeem_vk));
    }
}
