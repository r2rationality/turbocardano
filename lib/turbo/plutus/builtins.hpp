#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <functional>
#include <turbo/plutus/types.hpp>

namespace turbo::plutus {
    struct builtin_one_arg: std::function<value(allocator &, const value &)> {
        using std::function<value(allocator &, const value &)>::function;
    };
    struct builtin_two_arg: std::function<value(allocator &, const value &, const value &)> {
        using std::function<value(allocator &, const value &, const value &)>::function;
    };
    struct builtin_three_arg: std::function<value(allocator &, const value &, const value &, const value &)> {
        using std::function<value(allocator &, const value &, const value &, const value &)>::function;
    };
    struct builtin_six_arg: std::function<value(allocator &, const value &, const value &, const value &, const value &, const value &, const value &)> {
        using std::function<value(allocator &, const value &, const value &, const value &, const value &, const value &, const value &)>::function;
    };

    struct builtin_info {
        size_t num_args;
        builtin_any func;
        std::string name;
        size_t polymorphic_args = 0;
        size_t batch = 1;
    };
    using builtin_map = std::unordered_map<builtin_tag, builtin_info>;

    namespace builtins {
        extern value add_integer(allocator &, const value &x, const value &y);
        extern value subtract_integer(allocator &, const value &x, const value &y);
        extern value multiply_integer(allocator &, const value &x, const value &y);
        extern value divide_integer(allocator &, const value &x, const value &y);
        extern value mod_integer(allocator &, const value &x, const value &y);
        extern value quotient_integer(allocator &, const value &x, const value &y);
        extern value remainder_integer(allocator &, const value &x, const value &y);
        extern value equals_integer(allocator &, const value &x, const value &y);
        extern value less_than_integer(allocator &, const value &x, const value &y);
        extern value less_than_equals_integer(allocator &, const value &x, const value &y);
        extern value append_byte_string(allocator &, const value &x, const value &y);
        extern value cons_byte_string(allocator &, const value &c, const value &s);
        extern value cons_byte_string_v2(allocator &, const value &c, const value &s);
        extern value slice_byte_string(allocator &, const value &pos_raw, const value &sz_raw, const value &s_raw);
        extern value length_of_byte_string(allocator &, const value &s);
        extern value index_byte_string(allocator &, const value &s_t, const value &i_t);
        extern value equals_byte_string(allocator &, const value &s1, const value &s2);
        extern value less_than_byte_string(allocator &, const value &s1, const value &s2);
        extern value less_than_equals_byte_string(allocator &, const value &s1_t, const value &s2_t);
        extern value append_string(allocator &, const value &s1, const value &s2);
        extern value equals_string(allocator &, const value &s1, const value &s2);
        extern value encode_utf8(allocator &, const value &s);
        extern value decode_utf8(allocator &, const value &b);
        extern value if_then_else(allocator &, const value &condition, const value &yes, const value &no);
        extern value sha2_256(allocator &, const value &s);
        extern value sha3_256(allocator &, const value &s);
        extern value blake2b_256(allocator &, const value &s);
        extern value verify_ed25519_signature(allocator &, const value &sig, const value &msg, const value &vk);
        extern value choose_unit(allocator &, const value &u, const value &v);
        extern value fst_pair(allocator &, const value &p);
        extern value snd_pair(allocator &, const value &p);
        extern value choose_list(allocator &, const value &a, const value &t1, const value &t2);
        extern value mk_cons(allocator &, const value &x, const value &l);
        extern value head_list(allocator &, const value &l);
        extern value tail_list(allocator &, const value &l);
        extern value null_list(allocator &, const value &l);
        extern value trace(allocator &, const value &s, const value &t);
        extern value choose_data(allocator &, const value &d, const value &c, const value &m, const value &l, const value &i, const value &b);
        extern value constr_data(allocator &, const value &c, const value &l);
        extern value map_data(allocator &, const value &m);
        extern value list_data(allocator &, const value &m);
        extern value i_data(allocator &, const value &t);
        extern value b_data(allocator &, const value &t);
        extern value un_constr_data(allocator &, const value &t);
        extern value un_map_data(allocator &, const value &t);
        extern value un_list_data(allocator &, const value &t);
        extern value un_i_data(allocator &, const value &t);
        extern value un_b_data(allocator &, const value &t);
        extern value equals_data(allocator &, const value &d1, const value &d2);
        extern value mk_pair_data(allocator &, const value &fst, const value &snd);
        extern value mk_nil_data(allocator &, const value &);
        extern value mk_nil_pair_data(allocator &, const value &);
        extern value serialize_data(allocator &, const value &d);
        extern value verify_ecdsa_secp_256k1_signature(allocator &, const value &, const value &, const value &);
        extern value verify_schnorr_secp_256k1_signature(allocator &, const value &, const value &, const value &);
        extern value blake2b_224(allocator &, const value &);
        extern value keccak_256(allocator &, const value &);
        extern value integer_to_byte_string(allocator &, const value &, const value &, const value &);
        extern value byte_string_to_integer(allocator &, const value &, const value &);
        extern value bls12_381_g1_add(allocator &, const value &, const value &);
        extern value bls12_381_g1_neg(allocator &, const value &);
        extern value bls12_381_g1_scalar_mul(allocator &, const value &, const value &);
        extern value bls12_381_g1_equal(allocator &, const value &, const value &);
        extern value bls12_381_g1_hash_to_group(allocator &, const value &, const value &);
        extern value bls12_381_g1_compress(allocator &, const value &);
        extern value bls12_381_g1_uncompress(allocator &, const value &);
        extern value bls12_381_g2_add(allocator &, const value &, const value &);
        extern value bls12_381_g2_neg(allocator &, const value &);
        extern value bls12_381_g2_scalar_mul(allocator &, const value &, const value &);
        extern value bls12_381_g2_equal(allocator &, const value &, const value &);
        extern value bls12_381_g2_hash_to_group(allocator &, const value &, const value &);
        extern value bls12_381_g2_compress(allocator &, const value &);
        extern value bls12_381_g2_uncompress(allocator &, const value &);
        extern value bls12_381_miller_loop(allocator &, const value &, const value &);
        extern value bls12_381_mul_ml_result(allocator &, const value &, const value &);
        extern value bls12_381_final_verify(allocator &, const value &, const value &);
        extern value and_byte_string(allocator &, const value &, const value &, const value &);
        extern value or_byte_string(allocator &, const value &, const value &, const value &);
        extern value xor_byte_string(allocator &, const value &, const value &, const value &);
        extern value complement_byte_string(allocator &, const value &);
        extern value shift_byte_string(allocator &, const value &, const value &);
        extern value rotate_byte_string(allocator &, const value &, const value &);
        extern value count_set_bits(allocator &, const value &);
        extern value find_first_set_bit(allocator &, const value &);
        extern value read_bit(allocator &, const value &, const value &);
        extern value write_bits(allocator &, const value &, const value &, const value &);
        extern value replicate_byte(allocator &, const value &, const value &);
        extern value ripemd_160(allocator &, const value &);
        extern value exp_mod_integer(allocator &, const value &, const value &, const value &);

        extern const builtin_map &semantics_v1();
        extern const builtin_map &semantics_v2();
    }
}