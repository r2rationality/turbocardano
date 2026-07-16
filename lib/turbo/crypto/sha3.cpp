/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <hash-library/sha3.h>
#include <turbo/crypto/sha3.hpp>

namespace turbo::crypto::sha3 {
    void digest(const std::span<uint8_t> &out, const buffer &in)
    {
        SHA3 sha3 {};
        sha3.add(in.data(), in.size());
        sha3.getHashBin(out);
    }
}