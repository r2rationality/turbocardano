/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

extern "C" {
#   include <sodium.h>
}
#include <turbo/crypto/ed25519.hpp>
#include <turbo/crypto/sha2.hpp>

namespace turbo::crypto::sha2 {
    void digest(const std::span<uint8_t> &out, const buffer &in)
    {
        if (out.size() != sizeof(hash_256)) [[unlikely]]
            throw error(fmt::format("output size must be {} but got {}", sizeof(hash_256), out.size()));
        crypto::ed25519::ensure_initialized();
        if (crypto_hash_sha256(out.data(), in.data(), in.size()) != 0) [[unlikely]]
            throw error("sha2 computation hash failed!");
    }
}
