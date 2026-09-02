#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <cstddef>
#include <cstdint>
#include <string>
#include <turbo/common/format.hpp>
#include <turbo/util.hpp>

namespace turbo::plutus {
    struct version {
        uint64_t major = 1;
        uint64_t minor = 1;
        uint64_t patch = 0;

        version() =default;
        version(const std::string &s);

        version(const uint64_t major_, const uint64_t minor_, const uint64_t patch_):
            major { major_ }, minor { minor_ }, patch { patch_ }
        {
        }

        bool operator>=(const version &o) const;
        bool operator==(const version &o) const;

        bool empty() const
        {
            return major == 0 && minor == 0 && patch == 0;
        }

        operator std::string() const
        {
            return fmt::format("{}.{}.{}", major, minor, patch);
        }

        bool operator>=(const std::string &s) const
        {
            const version o { s };
            return *this >= o;
        }
    };

    enum class term_tag: uint8_t {
        variable = 0,
        delay    = 1,
        lambda   = 2,
        apply    = 3,
        constant = 4,
        force    = 5,
        error    = 6,
        builtin  = 7,
        constr   = 8,
        acase    = 9
    };

    enum class type_tag: uint8_t {
        integer              = 0,
        bytestring           = 1,
        string               = 2,
        unit                 = 3,
        boolean              = 4,
        list                 = 5,
        pair                 = 6,
        application          = 7,
        data                 = 8,
        bls12_381_g1_element = 9,
        bls12_381_g2_element = 10,
        bls12_381_ml_result   = 11,
        array                 = 12,
        value                 = 13
    };

    enum class builtin_tag: uint8_t {
        add_integer = 0,
        subtract_integer = 1,
        multiply_integer = 2,
        divide_integer = 3,
        quotient_integer = 4,
        remainder_integer = 5,
        mod_integer = 6,
        equals_integer = 7,
        less_than_integer = 8,
        less_than_equals_integer = 9,
        append_byte_string = 10,
        cons_byte_string = 11,
        slice_byte_string = 12,
        length_of_byte_string = 13,
        index_byte_string = 14,
        equals_byte_string = 15,
        less_than_byte_string = 16,
        less_than_equals_byte_string = 17,
        sha2_256 = 18,
        sha3_256 = 19,
        blake2b_256 = 20,
        verify_ed25519_signature = 21,
        append_string = 22,
        equals_string = 23,
        encode_utf8 = 24,
        decode_utf8 = 25,
        if_then_else = 26,
        choose_unit = 27,
        trace = 28,
        fst_pair = 29,
        snd_pair = 30,
        choose_list = 31,
        mk_cons = 32,
        head_list = 33,
        tail_list = 34,
        null_list = 35,
        choose_data = 36,
        constr_data = 37,
        map_data = 38,
        list_data = 39,
        i_data = 40,
        b_data = 41,
        un_constr_data = 42,
        un_map_data = 43,
        un_list_data = 44,
        un_i_data = 45,
        un_b_data = 46,
        equals_data = 47,
        mk_pair_data = 48,
        mk_nil_data = 49,
        mk_nil_pair_data = 50,
        // Plutus v2
        serialise_data = 51,
        verify_ecdsa_secp_256k1_signature = 52,
        verify_schnorr_secp_256k1_signature = 53,
        // Plutus v3
        bls12_381_g1_add = 54,
        bls12_381_g1_neg = 55,
        bls12_381_g1_scalar_mul = 56,
        bls12_381_g1_equal = 57,
        bls12_381_g1_compress = 58,
        bls12_381_g1_uncompress = 59,
        bls12_381_g1_hash_to_group = 60,
        bls12_381_g2_add = 61,
        bls12_381_g2_neg = 62,
        bls12_381_g2_scalar_mul = 63,
        bls12_381_g2_equal = 64,
        bls12_381_g2_compress = 65,
        bls12_381_g2_uncompress = 66,
        bls12_381_g2_hash_to_group = 67,
        bls12_381_miller_loop = 68,
        bls12_381_mul_ml_result = 69,
        bls12_381_final_verify = 70,
        keccak_256 = 71,
        blake2b_224 = 72,
        integer_to_byte_string = 73,
        byte_string_to_integer = 74,
        // Future
        and_byte_string = 75,
        or_byte_string = 76,
        xor_byte_string = 77,
        complement_byte_string = 78,
        read_bit = 79,
        write_bits = 80,
        replicate_byte = 81,
        shift_byte_string = 82,
        rotate_byte_string = 83,
        count_set_bits = 84,
        find_first_set_bit = 85,
        ripemd_160 = 86,
        exp_mod_integer = 87,
        drop_list = 88,
        length_of_array = 89,
        list_to_array = 90,
        index_array = 91,
        bls12_381_g1_multi_scalar_mul = 92,
        bls12_381_g2_multi_scalar_mul = 93,
        insert_coin = 94,
        lookup_coin = 95,
        union_value = 96,
        value_contains = 97,
        value_data = 98,
        un_value_data = 99,
        scale_value = 100
    };

    static constexpr size_t builtin_tag_count = static_cast<size_t>(builtin_tag::scale_value) + 1;
}

namespace fmt {
    template<>
    struct formatter<turbo::plutus::term_tag>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::term_tag &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using term = turbo::plutus::term_tag;
            switch (v) {
                case term::variable: return fmt::format_to(ctx.out(), "term::variable");
                case term::delay: return fmt::format_to(ctx.out(), "term::delay");
                case term::lambda: return fmt::format_to(ctx.out(), "term::lambda");
                case term::apply: return fmt::format_to(ctx.out(), "term::apply");
                case term::constant: return fmt::format_to(ctx.out(), "term::constant");
                case term::force: return fmt::format_to(ctx.out(), "term::force");
                case term::error: return fmt::format_to(ctx.out(), "term::error");
                case term::builtin: return fmt::format_to(ctx.out(), "term::builtin");
                case term::constr: return fmt::format_to(ctx.out(), "term::constr");
                case term::acase: return fmt::format_to(ctx.out(), "term::case");
                [[unlikely]] default: return fmt::format_to(ctx.out(), "term::unknown({})", static_cast<int>(v));
            }
        }
    };

    template<>
    struct formatter<turbo::plutus::type_tag>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::type_tag &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using type = turbo::plutus::type_tag;
            switch (v) {
                case type::integer: return fmt::format_to(ctx.out(), "integer");
                case type::bytestring: return fmt::format_to(ctx.out(), "bytestring");
                case type::string: return fmt::format_to(ctx.out(), "string");
                case type::unit: return fmt::format_to(ctx.out(), "unit");
                case type::boolean: return fmt::format_to(ctx.out(), "bool");
                case type::list: return fmt::format_to(ctx.out(), "list");
                case type::pair: return fmt::format_to(ctx.out(), "pair");
                case type::application: return fmt::format_to(ctx.out(), "apply");
                case type::data: return fmt::format_to(ctx.out(), "data");
                case type::bls12_381_g1_element: return fmt::format_to(ctx.out(), "bls12_381_g1_element");
                case type::bls12_381_g2_element: return fmt::format_to(ctx.out(), "bls12_381_g2_element");
                case type::bls12_381_ml_result: return fmt::format_to(ctx.out(), "bls12_381_ml_result");
                case type::array: return fmt::format_to(ctx.out(), "array");
                case type::value: return fmt::format_to(ctx.out(), "value");
                [[unlikely]] default: throw turbo::error(fmt::format("unknown type: {}", static_cast<int>(v)));
            }
        }
    };
}
