/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

extern "C" {
#   include <sodium.h>
};
#include <turbo/common/mutex.hpp>
#include "ed25519.hpp"

namespace turbo::crypto::ed25519 {
    struct sodium_initializer {
        sodium_initializer() {
            if (sodium_init() == -1)
                throw error("Failed to initialize libsodium!");
        }
    };

    void ensure_initialized()
    {
        // will be initialized on the first call, after that do nothing
        static sodium_initializer init {};
    }

    void create(const std::span<uint8_t> &sk, const std::span<uint8_t> &vk)
    {
        if (sk.size() != sizeof(skey))
            throw error(fmt::format("private key must have {} bytes but got: {}!", sizeof(skey), sk.size()));
        if (vk.size() != sizeof(vkey))
            throw error(fmt::format("verification key must have {} bytes but got: {}!", sizeof(vkey), vk.size()));
        ensure_initialized();
        if (crypto_sign_keypair(vk.data(), sk.data()) != 0)
            throw error("failed to generate a cryptographic key pair!");
    }

    void create_from_seed(const std::span<uint8_t> &sk, const std::span<uint8_t> &vk, const buffer &sd)
    {
        if (sk.size() != sizeof(skey))
            throw error(fmt::format("private key must have {} bytes but got: {}!", sizeof(skey), sk.size()));
        if (vk.size() != sizeof(vkey))
            throw error(fmt::format("verification key must have {} bytes but got: {}!", sizeof(vkey), vk.size()));
        if (sd.size() != sizeof(seed))
            throw error(fmt::format("seed must have {} bytes but got: {}!", sizeof(seed), sd.size()));
        ensure_initialized();
        if (crypto_sign_seed_keypair(vk.data(), sk.data(), sd.data()) != 0)
            throw error("failed to generate a cryptographic key pair!");
    }

    std::pair<skey, vkey> create_from_seed(const buffer &seed)
    {
        skey sk {};
        vkey vk {};
        create_from_seed(sk, vk, seed);
        return std::make_pair(sk, vk);
    }

    // a convenience method
    skey create_sk_from_seed(const buffer &sd)
    {
        skey sk {};
        vkey vk {};
        if (sd.size() != sizeof(seed))
            throw error(fmt::format("seed must have {} bytes but got: {}!", sizeof(seed), sd.size()));
        create_from_seed(sk, vk, sd);
        return sk;
    }

    void extract_vk(const std::span<uint8_t> &vk, const buffer &sk)
    {
        if (sk.size() != sizeof(skey))
            throw error(fmt::format("private key must have {} bytes but got: {}!", sizeof(skey), sk.size()));
        if (vk.size() != sizeof(vkey))
            throw error(fmt::format("verification key must have {} bytes but got: {}!", sizeof(vkey), vk.size()));
        if (crypto_sign_ed25519_sk_to_pk(vk.data(), sk.data()) != 0)
            throw error("failed to extract the verification key from a secret key!");
    }

    vkey extract_vk(const buffer &sk)
    {
        vkey vk {};
        extract_vk(vk, sk);
        return vk;
    }

    void sign(const std::span<uint8_t> &sig, const buffer &msg, const buffer &sk)
    {
        if (sk.size() != sizeof(skey))
            throw error(fmt::format("private key must have {} bytes but got: {}!", sizeof(skey), sk.size()));
        if (sig.size() != sizeof(signature))
            throw error(fmt::format("signature buffer must have {} bytes but got: {}!", sizeof(signature), sig.size()));
        ensure_initialized();
        if (crypto_sign_detached(sig.data(), NULL, msg.data(), msg.size(), sk.data()) != 0)
            throw error("failed to cryptographically sign a message!");
    }

    signature sign(const buffer &msg, const buffer &sk)
    {
        signature sig {};
        sign(sig, msg, sk);
        return sig;
    }

    bool verify(const buffer &sig, const buffer &vk, const buffer &msg)
    {
        if (sig.size() != sizeof(signature))
            throw error(fmt::format("signature must have {} bytes but got: {}!", sizeof(signature), sig.size()));
        if (vk.size() != sizeof(vkey))
            throw error(fmt::format("public key must have {} bytes but got: {}!", sizeof(vkey), vk.size()));
        ensure_initialized();
        return crypto_sign_verify_detached(sig.data(), msg.data(), msg.size(), vk.data()) == 0;
    }
}