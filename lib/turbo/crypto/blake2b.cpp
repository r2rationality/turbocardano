/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

extern "C" {
#   include <sodium.h>
}
#include <turbo/crypto/blake2b.hpp>
#include <turbo/crypto/ed25519.hpp>

namespace turbo::crypto::blake2b {
    void digest(const std::span<uint8_t> &out, const buffer &in)
    {
        ed25519::ensure_initialized();
        if (crypto_generichash(out.data(), out.size(), in.data(), in.size(), nullptr, 0) != 0) [[unlikely]]
            throw error("libsodium error: can't compute hash!");
    }
}
