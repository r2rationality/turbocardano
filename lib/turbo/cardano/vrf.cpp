/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

extern "C" {
#   include <vrf03/vrf.h>
}
#include <optional>
#include <turbo/crypto/blake2b.hpp>
#include <turbo/common/logger.hpp>
#include <turbo/math/big-int.hpp>
#include <turbo/math/rational.hpp>
#include "vrf.hpp"

namespace turbo::cardano {
    namespace {
        // Cardano's FixedPoint is Data.Fixed with 34 decimal places.  These
        // operations preserve Data.Fixed's floor division, including for
        // negative values.
        class fixed_34 {
            static const cpp_int &_resolution()
            {
                static const cpp_int val { "10000000000000000000000000000000000" };
                return val;
            }

            static cpp_int _div(cpp_int num, const cpp_int &den)
            {
                cpp_int quotient = num / den;
                const cpp_int remainder = num % den;
                if (remainder != 0 && ((remainder < 0) != (den < 0)))
                    --quotient;
                return quotient;
            }

        public:
            cpp_int raw {};

            static fixed_34 integer(const cpp_int &val)
            {
                return { val * _resolution() };
            }

            static fixed_34 rational(const cpp_int &num, const cpp_int &den)
            {
                return { _div(num * _resolution(), den) };
            }

            friend fixed_34 operator+(const fixed_34 &a, const fixed_34 &b)
            {
                return { a.raw + b.raw };
            }

            friend fixed_34 operator-(const fixed_34 &a, const fixed_34 &b)
            {
                return { a.raw - b.raw };
            }

            friend fixed_34 operator-(const fixed_34 &a)
            {
                return { -a.raw };
            }

            friend fixed_34 operator*(const fixed_34 &a, const fixed_34 &b)
            {
                return { _div(a.raw * b.raw, _resolution()) };
            }

            friend fixed_34 operator/(const fixed_34 &a, const fixed_34 &b)
            {
                return { _div(a.raw * _resolution(), b.raw) };
            }

            friend bool operator<(const fixed_34 &a, const fixed_34 &b)
            {
                return a.raw < b.raw;
            }

            friend bool operator<=(const fixed_34 &a, const fixed_34 &b)
            {
                return a.raw <= b.raw;
            }

            friend bool operator>=(const fixed_34 &a, const fixed_34 &b)
            {
                return a.raw >= b.raw;
            }

            fixed_34 abs() const
            {
                return raw < 0 ? -*this : *this;
            }
        };

        fixed_34 _ipow_positive(const fixed_34 &base, const uint64_t exponent)
        {
            if (!exponent)
                return fixed_34::integer(1);
            if (!(exponent % 2)) {
                const auto half = _ipow_positive(base, exponent / 2);
                return half * half;
            }
            return base * _ipow_positive(base, exponent - 1);
        }

        fixed_34 _ipow(const fixed_34 &base, const int64_t exponent)
        {
            if (exponent < 0)
                return fixed_34::integer(1) / _ipow_positive(base, static_cast<uint64_t>(-exponent));
            return _ipow_positive(base, static_cast<uint64_t>(exponent));
        }

        const fixed_34 &_exp_one()
        {
            static const fixed_34 val = [] {
                const auto one = fixed_34::integer(1);
                const auto epsilon = fixed_34::rational(
                    1, cpp_int { "1000000000000000000000000" });
                auto last = one;
                auto acc = one;
                auto divisor = one;
                for (size_t n = 1; n < 1000; ++n) {
                    const auto next = last / divisor;
                    if (next.abs() < epsilon)
                        break;
                    last = next;
                    acc = acc + next;
                    divisor = divisor + one;
                }
                return acc;
            }();
            return val;
        }

        int64_t _find_e(const fixed_34 &x)
        {
            const auto &e = _exp_one();
            auto lower = int64_t { -1 };
            auto upper = int64_t { 1 };
            auto lower_value = fixed_34::integer(1) / e;
            auto upper_value = e;
            while (!(lower_value <= x && x <= upper_value)) {
                lower_value = lower_value * lower_value;
                upper_value = upper_value * upper_value;
                lower *= 2;
                upper *= 2;
            }
            while (lower + 1 != upper) {
                const auto middle = lower + (upper - lower) / 2;
                if (x < _ipow(e, middle))
                    upper = middle;
                else
                    lower = middle;
            }
            return lower;
        }

        fixed_34 _ln_cf(const fixed_34 &x)
        {
            const auto zero = fixed_34::integer(0);
            const auto one = fixed_34::integer(1);
            const auto epsilon = fixed_34::rational(
                1, cpp_int { "1000000000000000000000000" });
            auto a_nm2 = one;
            auto b_nm2 = zero;
            auto a_nm1 = zero;
            auto b_nm1 = one;
            auto previous = zero;
            bool have_previous = false;
            for (size_t n = 0; n <= 1000; ++n) {
                const auto k = (n + 1) / 2;
                const auto an = n ? fixed_34::integer(cpp_int { k * k }) * x : x;
                const auto bn = fixed_34::integer(cpp_int { n + 1 });
                const auto a_n = bn * a_nm1 + an * a_nm2;
                const auto b_n = bn * b_nm1 + an * b_nm2;
                const auto value = a_n / b_n;
                if (n == 1000 || (have_previous && (previous - value).abs() < epsilon))
                    return value;
                have_previous = true;
                previous = value;
                a_nm2 = a_nm1;
                b_nm2 = b_nm1;
                a_nm1 = a_n;
                b_nm1 = b_n;
            }
            throw error("unreachable fixed-point logarithm state");
        }

        fixed_34 _ln(const fixed_34 &x)
        {
            if (x <= fixed_34::integer(0)) [[unlikely]]
                throw error("fixed-point logarithm input must be positive");
            const auto exponent = _find_e(x);
            const auto remainder = x / _ipow(_exp_one(), exponent) - fixed_34::integer(1);
            return fixed_34::integer(exponent) + _ln_cf(remainder);
        }

        fixed_34 _active_slot_log(const rational_u64 &f)
        {
            const auto f_fixed = fixed_34::rational(f.numerator, f.denominator);
            // floor(fpPrecision * ln(1 - f)) is the raw FixedPoint value;
            // activeSlotLog converts that integer back to the same FixedPoint.
            return _ln(fixed_34::integer(1) - f_fixed);
        }

        const fixed_34 &_cached_active_slot_log(const rational_u64 &f)
        {
            struct cache {
                rational_u64 coefficient;
                fixed_34 log;
            };
            thread_local std::optional<cache> cached {};
            if (!cached || cached->coefficient != f)
                cached.emplace(cache { f, _active_slot_log(f) });
            return cached->log;
        }

        bool _taylor_exp_below(const fixed_34 &reference, const fixed_34 &x)
        {
            // taylorExpCmp 3: only BELOW accepts; ABOVE and MaxReached reject.
            const auto one = fixed_34::integer(1);
            const auto bound = fixed_34::integer(3);
            auto error = x;
            auto approximation = one;
            auto divisor = one;
            for (size_t n = 0; n < 1000; ++n) {
                const auto next_divisor = divisor + one;
                const auto next = error;
                const auto next_error = error * x / next_divisor;
                const auto next_approximation = approximation + next;
                const auto error_bound = (next_error * bound).abs();
                if (reference >= next_approximation + error_bound)
                    return false;
                if (reference < next_approximation - error_bound)
                    return true;
                error = next_error;
                approximation = next_approximation;
                divisor = next_divisor;
            }
            return false;
        }
    }

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
        if (nonce.size() != 32) [[unlikely]] throw error(fmt::format("nonce must be of 32 bytes but got {}!", nonce.size()));
        memcpy(data.data() + 8, nonce.data(), nonce.size());
        return crypto::blake2b::digest(data);
    }

    vrf_nonce vrf_make_seed(const buffer &uc_nonce, uint64_t slot, const buffer &nonce)
    {
        byte_array<8 + 32> data {};
        uint64_t be_slot = host_to_net<uint64_t>(slot);
        static_assert(8 == sizeof(be_slot), "uint64_t must be 8 bytes");
        memcpy(data.data(), &be_slot, sizeof(be_slot));
        if (nonce.size() != 32) [[unlikely]] throw error(fmt::format("nonce must be of 32 bytes but got {}!", nonce.size()));
        memcpy(data.data() + 8, nonce.data(), nonce.size());
        vrf_nonce seed_tmp = crypto::blake2b::digest(data);
        if (uc_nonce.size() != seed_tmp.size()) [[unlikely]] throw error(fmt::format("uc_nonce must be of {} bytes but got {}!", seed_tmp.size(), uc_nonce.size()));
        for (size_t i = 0; i < seed_tmp.size(); ++i)
            seed_tmp[i] ^= uc_nonce[i];
        return seed_tmp;
    }

    vrf_nonce vrf_extended_hash(const buffer &result, uint8_t extension)
    {
        byte_array<65> data;
        if (result.size() != 64) [[unlikely]] throw error(fmt::format("result must be 64 bytes but got {}!", result.size()));
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
        if (nonce_prev.size() != 32) [[unlikely]] throw error(fmt::format("prev_nonce must be of 32 bytes but got {}!", nonce_prev.size()));
        if (nonce_new.size() != 32) [[unlikely]] throw error(fmt::format("prev_nonce must be of 32 bytes but got {}!", nonce_new.size()));
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
        if (exp_res.size() != sizeof(vrf_result)) [[unlikely]]
            throw error(fmt::format("result must be {} bytes but got {}!", sizeof(vrf_result), exp_res.size()));
        if (vkey.size() != sizeof(vrf_vkey)) [[unlikely]]
            throw error(fmt::format("vkey must be {} bytes but got {}!", sizeof(vrf_vkey), vkey.size()));
        if (proof.size() != sizeof(vrf_proof)) [[unlikely]]
            throw error(fmt::format("proof must be {} bytes but got {}!", sizeof(vrf_proof), proof.size()));
        vrf_result res;
        bool ok = crypto_vrf_ietfdraft03_verify(res.data(), vkey.data(), proof.data(), msg.data(), msg.size()) == 0;
        if (ok)
            ok = memcmp(res.data(), exp_res.data(), res.size()) == 0;
        return ok;
    }

    void vrf03_prove(const write_buffer &proof, const write_buffer &result, const buffer &sk, const buffer &msg)
    {
        if (proof.size() != sizeof(vrf_proof)) [[unlikely]]
            throw error(fmt::format("proof must be {} bytes but got {}!", sizeof(vrf_proof), proof.size()));
        if (result.size() != sizeof(vrf_result)) [[unlikely]]
            throw error(fmt::format("seed must be {} bytes but got {}!", sizeof(vrf_result), result.size()));
        if (sk.size() != sizeof(vrf_skey)) [[unlikely]]
            throw error(fmt::format("skey must be {} bytes but got {}!", sizeof(vrf_skey), sk.size()));
        if (crypto_vrf_ietfdraft03_prove(proof.data(), sk.data(), msg.data(), msg.size()) != 0) [[unlikely]]
            throw error("VRF prove failed!");
        if (crypto_vrf_ietfdraft03_proof_to_hash(result.data(), proof.data()) != 0) [[unlikely]]
            throw error("VRF output generation failed!");
    }

    void vrf03_create(const write_buffer &sk, const write_buffer &vk)
    {
        if (vk.size() != sizeof(vrf_vkey)) [[unlikely]]
            throw error(fmt::format("vkey must be {} bytes but got {}!", sizeof(vrf_vkey), vk.size()));
        if (sk.size() != sizeof(vrf_skey)) [[unlikely]]
            throw error(fmt::format("skey must be {} bytes but got {}!", sizeof(vrf_skey), sk.size()));
        if (crypto_vrf_ietfdraft03_keypair(vk.data(), sk.data()) != 0) [[unlikely]]
            throw error("VRF keypair generation failed!");
    }

    void vrf03_create_from_seed(const write_buffer &sk, const write_buffer &vk, const buffer &seed)
    {
        if (vk.size() != sizeof(vrf_vkey)) [[unlikely]]
            throw error(fmt::format("vkey must be {} bytes but got {}!", sizeof(vrf_vkey), vk.size()));
        if (sk.size() != sizeof(vrf_skey)) [[unlikely]]
            throw error(fmt::format("skey must be {} bytes but got {}!", sizeof(vrf_skey), sk.size()));
        if (seed.size() != sizeof(vrf_seed)) [[unlikely]]
            throw error(fmt::format("seed must be {} bytes but got {}!", sizeof(vrf_seed), seed.size()));
        if (crypto_vrf_ietfdraft03_keypair_from_seed(vk.data(), sk.data(), seed.data()) != 0) [[unlikely]]
            throw error("VRF keypair generation failed!");
    }

    void vrf03_extract_vk(const write_buffer &vk, const buffer &sk)
    {
        if (vk.size() != sizeof(vrf_vkey)) [[unlikely]]
            throw error(fmt::format("vkey must be {} bytes but got {}!", sizeof(vrf_vkey), vk.size()));
        if (sk.size() != sizeof(vrf_skey)) [[unlikely]]
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

    bool vrf_leader_is_eligible(const buffer &result, const rational_u64 &f, const rational_u64 &leader_stake_rel)
    {
        if (result.size() != sizeof(vrf_result) && result.size() != sizeof(vrf_nonce)) [[unlikely]]
            throw error(fmt::format("vrf result must have {} or {} bytes but got {}!", sizeof(vrf_result), sizeof(vrf_nonce), result.size()));
        if (!f.denominator || !f.numerator || f.numerator > f.denominator || !leader_stake_rel.denominator
                || leader_stake_rel.numerator > leader_stake_rel.denominator) [[unlikely]]
            throw error("invalid leader-eligibility probability");
        if (f.numerator == f.denominator)
            return true;
        static const cpp_int max_result = cpp_int { 1 } << (8 * sizeof(vrf_result));
        static const cpp_int max_nonce = cpp_int { 1 } << (8 * sizeof(vrf_nonce));
        const auto &max_val = result.size() == sizeof(vrf_result) ? max_result : max_nonce;
        const auto leader_val = vrf_leader_value_nat(result);
        const cpp_int denominator = max_val - leader_val;
        const auto reciprocal = denominator
            ? fixed_34::rational(max_val, denominator)
            : fixed_34::integer(max_val);
        const auto stake = fixed_34::rational(
            leader_stake_rel.numerator, leader_stake_rel.denominator);
        const auto x = (-stake) * _cached_active_slot_log(f);
        const auto ok = _taylor_exp_below(reciprocal, x);
        if (!ok)
            logger::debug("failed leadership eligibility check: leader value: {} relative stake: {}",
                leader_val, leader_stake_rel);
        return ok;
    }
}
