/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */
#ifndef DAEDALUS_TURBO_CRYPTO_CRC32_HPP
#define DAEDALUS_TURBO_CRYPTO_CRC32_HPP

#include <hash-library/crc32.h>
#include <dt/util.hpp>

namespace daedalus_turbo::crypto::crc32
{
    using hash_32 = uint32_t;

    inline hash_32 digest(const buffer in)
    {
        hash_32 hash {};
        CRC32 crc {};
        crc.add(in.data(), in.size());
        crc.getHash(&hash);
        return hash;
    }
}

#endif // !DAEDALUS_TURBO_CRYPTO_CRC32_HPP