#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/common/bytes.hpp>

namespace turbo {
    struct rational_u64;
}

namespace turbo::cardano {
    using vrf_result = byte_array<64>;
    using vrf_skey = secure_byte_array<64>;
    using vrf_vkey = byte_array<32>;
    using vrf_proof = byte_array<80>;
    using vrf_seed = secure_byte_array<32>;
    using vrf_nonce = byte_array<32>;

    extern vrf_nonce vrf_make_input(uint64_t slot, const buffer &nonce);
    extern vrf_nonce vrf_make_seed(const buffer &uc_nonce, uint64_t slot, const buffer &nonce);
    extern vrf_nonce vrf_extended_hash(const buffer &result, uint8_t extension);
    extern vrf_nonce vrf_nonce_value(const buffer &result);
    extern vrf_nonce vrf_leader_value(const buffer &result);
    extern void vrf_nonce_accumulate(const std::span<uint8_t> &output, const buffer &nonce_prev, const buffer &nonce_new);
    extern vrf_nonce vrf_nonce_accumulate(const buffer &nonce_prev, const buffer &nonce_new);
    extern bool vrf03_verify(const buffer &exp_res, const buffer &vkey, const buffer &proof, const buffer &msg);
    extern void vrf03_prove(const write_buffer &proof, const write_buffer &result, const buffer &sk, const buffer &msg);
    extern void vrf03_create(const write_buffer &sk, const write_buffer &vk);
    extern void vrf03_create_from_seed(const write_buffer &sk, const write_buffer &vk, const buffer &seed);
    extern void vrf03_extract_vk(const write_buffer &vk, const buffer &sk);
    extern vrf_vkey vrf03_extract_vk(const buffer &sk);
    extern vrf_skey vrf03_create_sk_from_seed(const buffer &seed);
    extern bool vrf_leader_is_eligible(const buffer &result, const double f, const rational_u64 &leader_stake_rel);
}