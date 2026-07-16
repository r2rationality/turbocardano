/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <secp256k1.h>
#include <secp256k1_schnorrsig.h>
#include <turbo/common/test.hpp>
#include <turbo/crypto/secp256k1.hpp>

namespace turbo::crypto::secp256k1 {
    struct context {
        static const secp256k1_context *verify()
        {
            static context ctx { SECP256K1_CONTEXT_VERIFY };
            return ctx.ptr();
        }

        context(const unsigned flags): _ctx { secp256k1_context_create(flags) }
        {
            if (!_ctx)
                throw error("failure to create a SECP256K1 context");
        }

        ~context()
        {
            secp256k1_context_destroy(_ctx);
        }

        const secp256k1_context *ptr()
        {
            return _ctx;
        }
    private:
        secp256k1_context *_ctx;
    };

    namespace ecdsa {
        bool verify(const buffer &sig, const buffer &vk, const buffer &msg)
        {
            if (const auto exp_size = 64; sig.size() != exp_size)
                throw error(fmt::format("ECDSA signature size must have {} bytes but got {}", exp_size, sig.size()));
            if (const auto exp_size = 33; vk.size() != exp_size)
                throw error(fmt::format("ECDSA public key must have {} bytes but got {}", exp_size, vk.size()));
            if (const auto exp_size = 32; msg.size() != exp_size)
                throw error(fmt::format("ECDSA message hash size must have {} bytes but got {}", exp_size, msg.size()));
            secp256k1_pubkey vk_parsed;
            if (!secp256k1_ec_pubkey_parse(context::verify(), &vk_parsed, vk.data(), vk.size()))
                throw error(fmt::format("failed to parse ECDSA signature: {}", sig));
            secp256k1_ecdsa_signature sig_parsed;
            if (!secp256k1_ecdsa_signature_parse_compact(context::verify(), &sig_parsed, sig.data()))
                throw error(fmt::format("failed to parse ECDSA signature: {}", sig));
            return secp256k1_ecdsa_verify(context::verify(), &sig_parsed, msg.data(), &vk_parsed) == 1;
        }
    }

    namespace schnorr {
        extern bool verify(const buffer &sig, const buffer &vk, const buffer &msg)
        {
            if (const auto exp_size = 64; sig.size() != exp_size)
                throw error(fmt::format("Schnorr signature size must have {} bytes but got {}", exp_size, sig.size()));
            if (const auto exp_size = 32; vk.size() != exp_size)
                throw error(fmt::format("Schnorr public key must have {} bytes but got {}", exp_size, vk.size()));
            secp256k1_xonly_pubkey vk_parsed;
            if (!secp256k1_xonly_pubkey_parse(context::verify(), &vk_parsed, vk.data()))
                throw error(fmt::format("public key is not valid: {}", vk));
            return secp256k1_schnorrsig_verify(context::verify(),sig.data(), msg.data(), msg.size(), &vk_parsed) == 1;
        }
    }
}