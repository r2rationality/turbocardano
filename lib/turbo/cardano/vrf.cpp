/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

extern "C" {
#   include <vrf03/vrf.h>
}
#include <turbo/crypto/blake2b.hpp>
#include <turbo/common/logger.hpp>
#include <turbo/math/big-float.hpp>
#include <turbo/math/big-int.hpp>
#include <turbo/math/rational.hpp>
#include "vrf.hpp"

namespace turbo::cardano {
    static_assert(sizeof(vrf_result) == crypto_vrf_ietfdraft03_OUTPUTBYTES);
    static_assert(sizeof(vrf_skey) == crypto_vrf_ietfdraft03_SECRETKEYBYTES);
    static_assert(sizeof(vrf_vkey) == crypto_vrf_ietfdraft03_PUBLICKEYBYTES);
    static_assert(sizeof(vrf_proof) == crypto_vrf_ietfdraft03_PROOFBYTES);
    static_assert(sizeof(vrf_seed) == crypto_vrf_ietfdraft03_SEEDBYTES);

    vrf_nonce vrf_make_input(uint64_t slot, const buffer &nonce)
    {
        byte_array<8 + 32> data {};
        uint64_t be_slot = host_to_net<uint64_t>(slot);
        static_assert(8 == sizeof(be_slot), "uint64_t must be 8 bytes");
        memcpy(data.data(), &be_slot, sizeof(be_slot));
        if (nonce.size() != 32) throw error(fmt::format("nonce must be of 32 bytes but got {}!", nonce.size()));
        memcpy(data.data() + 8, nonce.data(), nonce.size());
        return crypto::blake2b::digest(data);
    }

    vrf_nonce vrf_make_seed(const buffer &uc_nonce, uint64_t slot, const buffer &nonce)
    {
        byte_array<8 + 32> data {};
        uint64_t be_slot = host_to_net<uint64_t>(slot);
        static_assert(8 == sizeof(be_slot), "uint64_t must be 8 bytes");
        memcpy(data.data(), &be_slot, sizeof(be_slot));
        if (nonce.size() != 32) throw error(fmt::format("nonce must be of 32 bytes but got {}!", nonce.size()));
        memcpy(data.data() + 8, nonce.data(), nonce.size());
        vrf_nonce seed_tmp = crypto::blake2b::digest(data);
        if (uc_nonce.size() != seed_tmp.size()) throw error(fmt::format("uc_nonce must be of {} bytes but got {}!", seed_tmp.size(), uc_nonce.size()));
        for (size_t i = 0; i < seed_tmp.size(); ++i)
            seed_tmp[i] ^= uc_nonce[i];
        return seed_tmp;
    }

    vrf_nonce vrf_extended_hash(const buffer &result, uint8_t extension)
    {   
        byte_array<65> data;
        if (result.size() != 64) throw error(fmt::format("result must be 64 bytes but got {}!", result.size()));
        data[0] = extension;
        memcpy(data.data() + 1, result.data(), result.size());
        return crypto::blake2b::digest(data);
    }

    vrf_nonce vrf_nonce_value(const buffer &result)
    {
        return crypto::blake2b::digest<vrf_nonce>(vrf_extended_hash(result, 'N'));
    }

    vrf_nonce vrf_leader_value(const buffer &result)
    {
        return vrf_extended_hash(result, 'L');
    }

    static cpp_int vrf_leader_value_nat(const buffer &data)
    {
        cpp_int leader_val {};
        for (size_t i = 0; i < data.size(); ++i) {
            leader_val <<= 8;
            leader_val += *static_cast<const uint8_t*>(data.data() + i);
        }
        return leader_val;
    }

    void vrf_nonce_accumulate(const std::span<uint8_t> &output, const buffer &nonce_prev, const buffer &nonce_new)
    {
        if (nonce_prev.size() != 32) throw error(fmt::format("prev_nonce must be of 32 bytes but got {}!", nonce_prev.size()));
        if (nonce_new.size() != 32) throw error(fmt::format("prev_nonce must be of 32 bytes but got {}!", nonce_new.size()));
        byte_array<64> data;
        static_assert(sizeof(data) == 32 + 32);
        memcpy(data.data(), nonce_prev.data(), nonce_prev.size());
        memcpy(data.data() + nonce_prev.size(), nonce_new.data(), nonce_new.size());
        crypto::blake2b::digest(output, data);
    }

    vrf_nonce vrf_nonce_accumulate(const buffer &nonce_prev, const buffer &nonce_new)
    {
        vrf_nonce output;
        vrf_nonce_accumulate(output, nonce_prev, nonce_new);
        return output;
    }

    bool vrf03_verify(const buffer &exp_res, const buffer &vkey, const buffer &proof, const buffer &msg)
    {
        if (exp_res.size() != sizeof(vrf_result))
            throw error(fmt::format("result must be {} bytes but got {}!", sizeof(vrf_result), exp_res.size()));
        if (vkey.size() != sizeof(vrf_vkey))
            throw error(fmt::format("vkey must be {} bytes but got {}!", sizeof(vrf_vkey), vkey.size()));
        if (proof.size() != sizeof(vrf_proof))
            throw error(fmt::format("proof must be {} bytes but got {}!", sizeof(vrf_proof), proof.size()));
        vrf_result res;
        bool ok = crypto_vrf_ietfdraft03_verify(res.data(), vkey.data(), proof.data(), msg.data(), msg.size()) == 0;
        if (ok)
            ok = memcmp(res.data(), exp_res.data(), res.size()) == 0;
        return ok;
    }

    void vrf03_prove(const write_buffer &proof, const write_buffer &result, const buffer &sk, const buffer &msg)
    {
        if (proof.size() != sizeof(vrf_proof))
            throw error(fmt::format("proof must be {} bytes but got {}!", sizeof(vrf_proof), proof.size()));
        if (result.size() != sizeof(vrf_result))
            throw error(fmt::format("seed must be {} bytes but got {}!", sizeof(vrf_result), result.size()));
        if (sk.size() != sizeof(vrf_skey))
            throw error(fmt::format("skey must be {} bytes but got {}!", sizeof(vrf_skey), sk.size()));
        if (crypto_vrf_ietfdraft03_prove(proof.data(), sk.data(), msg.data(), msg.size()) != 0)
            throw error("VRF prove failed!");
        if (crypto_vrf_ietfdraft03_proof_to_hash(result.data(), proof.data()) != 0)
            throw error("VRF output generation failed!");
    }

    void vrf03_create(const write_buffer &sk, const write_buffer &vk)
    {
        if (vk.size() != sizeof(vrf_vkey))
            throw error(fmt::format("vkey must be {} bytes but got {}!", sizeof(vrf_vkey), vk.size()));
        if (sk.size() != sizeof(vrf_skey))
            throw error(fmt::format("skey must be {} bytes but got {}!", sizeof(vrf_skey), sk.size()));
        if (crypto_vrf_ietfdraft03_keypair(vk.data(), sk.data()) != 0)
            throw error("VRF keypair generation failed!");
    }

    void vrf03_create_from_seed(const write_buffer &sk, const write_buffer &vk, const buffer &seed)
    {
        if (vk.size() != sizeof(vrf_vkey))
            throw error(fmt::format("vkey must be {} bytes but got {}!", sizeof(vrf_vkey), vk.size()));
        if (sk.size() != sizeof(vrf_skey))
            throw error(fmt::format("skey must be {} bytes but got {}!", sizeof(vrf_skey), sk.size()));
        if (seed.size() != sizeof(vrf_seed))
            throw error(fmt::format("seed must be {} bytes but got {}!", sizeof(vrf_seed), seed.size()));
        if (crypto_vrf_ietfdraft03_keypair_from_seed(vk.data(), sk.data(), seed.data()) != 0)
            throw error("VRF keypair generation failed!");
    }

    void vrf03_extract_vk(const write_buffer &vk, const buffer &sk)
    {
        if (vk.size() != sizeof(vrf_vkey))
            throw error(fmt::format("vkey must be {} bytes but got {}!", sizeof(vrf_vkey), vk.size()));
        if (sk.size() != sizeof(vrf_skey))
            throw error(fmt::format("skey must be {} bytes but got {}!", sizeof(vrf_skey), sk.size()));
        // cannot fail
        crypto_vrf_ietfdraft03_sk_to_pk(vk.data(), sk.data());
    }

    vrf_vkey vrf03_extract_vk(const buffer &sk)
    {
        vrf_vkey vk {};
        vrf03_extract_vk(vk, sk);
        return vk;
    }

    vrf_skey vrf03_create_sk_from_seed(const buffer &seed)
    {
        vrf_skey sk {};
        vrf_vkey vk {};
        vrf03_create_from_seed(sk, vk, seed);
        return sk;
    }

    bool vrf_leader_is_eligible(const buffer &result, const double f, const rational_u64 &leader_stake_rel)
    {
        if (result.size() != sizeof(vrf_result) && result.size() != sizeof(vrf_nonce))
            throw error(fmt::format("vrf result must have {} or {} bytes but got {}!", sizeof(vrf_result), sizeof(vrf_nonce), result.size()));
        using boost::multiprecision::cpp_int;
        cpp_int max_val { 1 };
        max_val <<= 8 * result.size();
        auto leader_val = vrf_leader_value_nat(result);
        cpp_rational p { leader_val, max_val };
        const auto prob_bin = cpp_float { 1.0 } - f;
        const auto ls_bin = static_cast<cpp_float>(leader_stake_rel.numerator) / leader_stake_rel.denominator;
        const auto threshold_bin = boost::multiprecision::pow(prob_bin, ls_bin);
        const auto ok = static_cast<cpp_float>(p) < threshold_bin;
        if (!ok)
            logger::debug("failed leadership eligibility check: leader value: {} threshold: {}", p, threshold_bin);
        return ok;
    }
}