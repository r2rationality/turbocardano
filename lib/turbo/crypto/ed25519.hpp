#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/util.hpp>

namespace turbo::crypto::ed25519 {
    using vkey = byte_array<32>;
    using vkey_full = byte_array<64>;
    using skey = secure_byte_array<64>;
    using signature = byte_array<64>;
    using seed = secure_byte_array<32>;

    extern void ensure_initialized();
    extern void create(const std::span<uint8_t> &sk, const std::span<uint8_t> &vk);
    extern void create_from_seed(const std::span<uint8_t> &sk, const std::span<uint8_t> &vk, const buffer &sd);
    extern std::pair<skey, vkey> create_from_seed(const buffer &seed);
    extern skey create_sk_from_seed(const buffer &sd);
    extern void extract_vk(const std::span<uint8_t> &vk, const buffer &sk);
    extern vkey extract_vk(const buffer &sk);
    extern void sign(const std::span<uint8_t> &sig, const buffer &msg, const buffer &sk);
    extern signature sign(const buffer &msg, const buffer &sk);
    extern bool verify(const buffer &sig, const buffer &vk, const buffer &msg);
}