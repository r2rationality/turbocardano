/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <hash-library/keccak.h>
#include <dt/crypto/keccak.hpp>

namespace daedalus_turbo::crypto::keccak {
    void digest(const std::span<uint8_t> &out, const buffer &in)
    {
        Keccak hasher {};
        hasher.add(in.data(), in.size());
        hasher.getHashBin(out);
    }
}
