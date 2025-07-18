#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/common/bytes.hpp>

namespace turbo::crypto::ripemd_160
{
    using hash_t = byte_array<20>;

    extern void digest(const std::span<uint8_t> &out, const buffer &in);

    inline hash_t digest(const buffer &in)
    {
        hash_t out;
        digest(out, in);
        return out;
    }
}