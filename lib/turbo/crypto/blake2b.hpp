#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/common/bytes.hpp>

namespace turbo::crypto::blake2b
{
    using hash_8 = byte_array<8>;
    using hash_28 = byte_array<28>;
    using hash_32 = byte_array<32>;

    extern void digest(const std::span<uint8_t> &out, const buffer &in);

    template<typename T=hash_32>
    T digest(const buffer &in)
    {
        T out;
        digest(out, in);
        return out;
    }
}

namespace std {
    template<>
    struct hash<turbo::crypto::blake2b::hash_32> {
        size_t operator()(const turbo::crypto::blake2b::hash_32 &o) const noexcept
        {
            return *reinterpret_cast<const size_t *>(o.data());
        }
    };

    template<>
    struct hash<turbo::crypto::blake2b::hash_28> {
        size_t operator()(const turbo::crypto::blake2b::hash_28 &o) const noexcept
        {
            return *reinterpret_cast<const size_t *>(o.data());
        }
    };
}