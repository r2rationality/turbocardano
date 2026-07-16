#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <hash-library/crc32.h>
#include <turbo/util.hpp>

namespace turbo::crypto::crc32
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