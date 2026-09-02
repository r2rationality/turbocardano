/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <bit>
#include <ranges>
#include <turbo/crypto/blake2b.hpp>
#include <turbo/crypto/ed25519.hpp>
#include <turbo/crypto/keccak.hpp>
#include <turbo/crypto/ripemd-160.hpp>
#include <turbo/crypto/secp256k1.hpp>
#include <turbo/crypto/sha2.hpp>
#include <turbo/crypto/sha3.hpp>
#include <turbo/plutus/builtins.hpp>
#include <utfcpp/utf8.h>

namespace turbo::plutus::builtins {
    using namespace crypto;

    value add_integer(allocator &alloc, const value &x, const value &y)
    {
        return { alloc, bint_type { alloc, *x.as_int() + *y.as_int() } };
    }

    value subtract_integer(allocator &alloc, const value &x, const value &y)
    {
        return { alloc, bint_type { alloc, *x.as_int() - *y.as_int() } };
    }

    value multiply_integer(allocator &alloc, const value &x, const value &y)
    {
        return { alloc, bint_type { alloc, *x.as_int() * *y.as_int() } };
    }

    value divide_integer(allocator &alloc, const value &x, const value &y)
    {
        const auto &y_val = y.as_int();
        if (y_val == 0) [[unlikely]]
            throw error("division by zero is not allowed!");
        const auto &x_val = x.as_int();
        bint_type::value_type div, rem;
        boost::multiprecision::divide_qr(*x_val, *y_val, div, rem);
        if (rem != 0 && ((*x_val < 0) ^ (*y_val < 0)))
            --div;
        return { alloc, bint_type { alloc, std::move(div) } };
    }

    static cpp_int mod_integer_int(const cpp_int &x, const cpp_int &y)
    {
        return ((x % y) + y) % y;
    }

    value mod_integer(allocator &alloc, const value &x, const value &y)
    {
        const auto &y_val = y.as_int();
        if (y_val == 0) [[unlikely]]
            throw error("division by zero is not allowed!");
        const auto &x_val = x.as_int();
        return { alloc, bint_type { alloc, mod_integer_int(*x_val, *y_val) } };
    }

    value quotient_integer(allocator &alloc, const value &x, const value &y)
    {
        const auto &y_val = y.as_int();
        if (y_val == 0) [[unlikely]]
            throw error("division by zero is not allowed!");
        return { alloc, bint_type { alloc, *x.as_int() / *y_val } };
    }

    value remainder_integer(allocator &alloc, const value &x, const value &y)
    {
        const auto &y_val = y.as_int();
        if (y_val == 0) [[unlikely]]
            throw error("division by zero is not allowed!");
        return { alloc, bint_type { alloc, *x.as_int() % *y_val } };
    }

    value equals_integer(allocator &alloc, const value &x, const value &y)
    {
        return value::boolean(alloc, x.as_int() == y.as_int());
    }

    value less_than_integer(allocator &alloc, const value &x, const value &y)
    {
        return value::boolean(alloc, *x.as_int() < *y.as_int());
    }

    value less_than_equals_integer(allocator &alloc, const value &x, const value &y)
    {
        return value::boolean(alloc, *x.as_int() <= *y.as_int());
    }

    value append_byte_string(allocator &alloc, const value &x, const value &y)
    {
        bstr_type::value_type res { alloc };
        const auto &x_val = x.as_bstr();
        const auto &y_val = y.as_bstr();
        res.reserve(x_val->size() + y_val->size());
        res << *x_val <<* y_val;
        return { alloc, std::move(res) };
    }

    value cons_byte_string(allocator &alloc, const value &c, const value &s)
    {
        cpp_int c_val { *c.as_int() };
        const auto &s_val = s.as_bstr();
        bstr_type::value_type res { alloc };
        res.reserve(1 + s_val->size());
        if (c_val < 0 || c_val > 255)
            c_val = mod_integer_int(c_val, 256);
        res << static_cast<uint8_t>(c_val) << *s_val;
        return { alloc, std::move(res) };
    }

    value cons_byte_string_v2(allocator &alloc, const value &c, const value &s)
    {
        const auto &c_val = c.as_int();
        const auto &s_val = s.as_bstr();
        bstr_type::value_type res { alloc };
        res.reserve(1 + s_val->size());
        if (*c_val < 0 || *c_val > 255) [[unlikely]]
            throw error(fmt::format("cons_byte_string's first parameter must be between 0 and 255: {}!", c_val));
        res << static_cast<uint8_t>(*c_val) << *s_val;
        return { alloc, std::move(res) };
    }

    value slice_byte_string(allocator &alloc, const value &pos_raw, const value &sz_raw, const value &s_raw)
    {
        auto pos = static_cast<int64_t>(*pos_raw.as_int());
        auto sz = static_cast<int64_t>(*sz_raw.as_int());
        const auto &s = s_raw.as_bstr();
        const auto s_sz = static_cast<int64_t>(s->size());
        if (pos < 0)
            pos = 0;
        if (pos > s_sz)
            pos = s->size();
        if (pos + sz > s_sz)
            sz = s_sz - pos;
        if (pos + sz < pos)
            sz = 0;
        return { alloc, static_cast<buffer>(*s).subspan(pos, sz) };
    }

    value length_of_byte_string(allocator &alloc, const value &s)
    {
        return { alloc, bint_type { alloc, s.as_bstr()->size() } };
    }

    value index_byte_string(allocator &alloc, const value &s_t, const value &i_t)
    {
        const auto &s = s_t.as_bstr();
        const auto &i_bi = i_t.as_int();
        if (*i_bi < 0 || *i_bi >= std::numeric_limits<size_t>::max()) [[unlikely]]
            throw error(fmt::format("byte_string index out of the allowed range: {}", i_bi));
        const auto i = static_cast<size_t>(*i_bi);
        if (i >= s->size()) [[unlikely]]
            throw error(fmt::format("byte_string index too big: {}", i));
        return { alloc, bint_type { alloc, (*s)[i] } };
    }

    value equals_byte_string(allocator &alloc, const value &s1, const value &s2)
    {
        return value::boolean(alloc, s1.as_bstr() == s2.as_bstr());
    }

    value less_than_byte_string(allocator &alloc, const value &s1, const value &s2)
    {
        return value::boolean(alloc, *s1.as_bstr() < *s2.as_bstr());
    }

    value less_than_equals_byte_string(allocator &alloc, const value &s1_t, const value &s2_t)
    {
        const auto &s1 = s1_t.as_bstr();
        const auto &s2 = s2_t.as_bstr();
        return value::boolean(alloc, *s1 < *s2 || s1 == s2);
    }

    value append_string(allocator &alloc, const value &s1, const value &s2)
    {
        str_type::value_type res { *s1.as_str(), alloc.resource() };
        res += *s2.as_str();
        return { alloc, std::move(res) };
    }

    value equals_string(allocator &alloc, const value &s1, const value &s2)
    {
        return value::boolean(alloc, s1.as_str() == s2.as_str());
    }

    value encode_utf8(allocator &alloc, const value &s)
    {
        return { alloc, buffer { *s.as_str() } };
    }

    value decode_utf8(allocator &alloc, const value &b)
    {
        const auto s = b.as_bstr()->str();
        if (const auto it = utf8::find_invalid(s.begin(), s.end()); it == s.end()) [[likely]]
            return { alloc, str_type { alloc, s } };
        throw error(fmt::format("an invalid utf8 sequence: {}", b.as_bstr()));
    }

    value if_then_else(allocator &, const value &condition, const value &yes, const value &no)
    {
        if (const auto cond = condition.as_bool(); cond)
            return yes;
        return no;
    }

    value sha2_256(allocator &alloc, const value &s)
    {
        bstr_type::value_type res { alloc, sizeof(sha2::hash_256) };
        sha2::digest(res, *s.as_bstr());
        return { alloc, std::move(res) };
    }

    value sha3_256(allocator &alloc, const value &s)
    {
        bstr_type::value_type res { alloc, sizeof(sha3::hash_256) };
        sha3::digest(res, *s.as_bstr());
        return { alloc, std::move(res) };
    }

    value blake2b_256(allocator &alloc, const value &s)
    {
        bstr_type::value_type res { alloc, sizeof(blake2b::hash_32) };
        blake2b::digest(res, *s.as_bstr());
        return { alloc, std::move(res) };
    }

    value blake2b_224(allocator &alloc, const value &s)
    {
        bstr_type::value_type res { alloc, sizeof(blake2b::hash_28) };
        blake2b::digest(res, *s.as_bstr());
        return { alloc, std::move(res) };
    }

    value keccak_256(allocator &alloc, const value &s)
    {
        bstr_type::value_type res { alloc, sizeof(keccak::hash_256) };
        keccak::digest(res, *s.as_bstr());
        return { alloc, std::move(res) };
    }

    value verify_ed25519_signature(allocator &alloc, const value &vk, const value &msg, const value &sig)
    {
        return value::boolean(alloc, ed25519::verify(*sig.as_bstr(), *vk.as_bstr(), *msg.as_bstr()));
    }

    value choose_unit(allocator &, const value &u, const value &v)
    {
        u.as_unit();
        return v;
    }

    value fst_pair(allocator &alloc, const value &p)
    {
        return { alloc, p.as_pair().first };
    }

    value snd_pair(allocator &alloc, const value &p)
    {
        return { alloc, p.as_pair().second };
    }

    value choose_list(allocator &, const value &a, const value &t1, const value &t2)
    {
        if (const auto &cl = a.as_list(); !cl.empty())
            return t2;
        return t1;
    }

    value mk_cons(allocator &alloc, const value &x, const value &l)
    {
        const auto &cx = x.as_const();
        const auto &cl = l.as_list();
        const auto cx_typ = constant_type::from_val(alloc, cx);
        if (cx_typ != cl.typ()) [[unlikely]]
            throw error(fmt::format("mkCons requires both arguments to be of the same type but got {} and {}", cx_typ, cl.typ()));
        return { alloc, constant { alloc, cl.prepend(alloc, cx) } };
    }

    value head_list(allocator &alloc, const value &l)
    {
        const auto &cl = l.as_list();
        if (!cl.empty()) [[likely]]
            return { alloc, cl.front() };
        throw error("head_list builtin called with an empty list!");
    }

    value tail_list(allocator &alloc, const value &l)
    {
        const auto &cl = l.as_list();
        if (cl.empty()) [[unlikely]]
            throw error("calling tail_list on an empty list!");
        return { alloc, constant { alloc, cl.drop(alloc, 1) } };
    }

    value null_list(allocator &alloc, const value &l)
    {
        return value::boolean(alloc, l.as_list().empty());
    }

    value trace(allocator &, const value &s, const value &t)
    {
        logger::trace("plutus builtins::trace: {}", s);
        return t;
    }

    value choose_data(allocator &, const value &d, const value &c, const value &m, const value &l, const value &i, const value &b)
    {
        return std::visit([&](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, data::int_type>) {
                return i;
            } else if constexpr (std::is_same_v<T, data::bstr_type>) {
                return b;
            } else if constexpr (std::is_same_v<T, data::list_type>) {
                return l;
            } else if constexpr (std::is_same_v<T, data::map_type>) {
                return m;
            } else if constexpr (std::is_same_v<T, data_constr>) {
                return c;
            } else {
                throw error(fmt::format("unsupported data type: {}!", typeid(T).name()));
            }
        }, *d.as_data());
    }

    value constr_data(allocator &alloc, const value &c, const value &l)
    {
        data::list_type dl { alloc };
        const auto &vals = l.as_list();
        dl.reserve(vals.size());
        vals.for_each([&](const auto &d) {
            dl.emplace_back(d.as_data());
        });
        return { alloc, data::constr(alloc, c.as_int(), std::move(dl)) };
    }

    value map_data(allocator &alloc, const value &m)
    {
        data::map_type dm { alloc };
        const auto &vals = m.as_list();
        dm.reserve(vals.size());
        vals.for_each([&](const auto &c) {
            const auto &p = c.as_pair();
            dm.emplace_back(data_pair { alloc, data { p.first.as_data() }, data { p.second.as_data() } });
        });
        return { alloc, data::map(alloc, std::move(dm)) };
    }

    value list_data(allocator &alloc, const value &l)
    {
        data::list_type dl { alloc };
        const auto &vals = l.as_list();
        dl.reserve(vals.size());
        vals.for_each([&](const auto &d) {
            dl.emplace_back(d.as_data());
        });
        return { alloc, data::list(alloc, std::move(dl)) };
    }

    value i_data(allocator &alloc, const value &i)
    {
        return { alloc, data::bint(alloc, bint_type { i.as_int() } ) };
    }

    value b_data(allocator &alloc, const value &b)
    {
        return { alloc, data::bstr(alloc, *b.as_bstr()) };
    }

    value un_constr_data(allocator &alloc, const value &t)
    {
        if (const auto &d = t.as_data(); std::holds_alternative<data_constr>(*d)) {
            const auto &c = std::get<data_constr>(*d);
            constant_list::list_type cl { alloc };
            cl.reserve(c->second.size());
            for (const auto &d: c->second)
                cl.emplace_back(alloc, d);
            return { alloc, constant { alloc, constant_pair { alloc, constant { alloc, bint_type { c->first } },
                constant { alloc, constant_list { alloc, constant_type { alloc, type_tag::data }, std::move(cl) } } } } };
        }
        throw error(fmt::format("invalid input for un_constr_data: {}!", t));
    }

    value un_map_data(allocator &alloc, const value &t)
    {
        if (const auto &d = t.as_data(); std::holds_alternative<data::map_type>(*d)) {
            const auto &m = std::get<data::map_type>(*d);
            constant_list::list_type cl { alloc };
            cl.reserve(m.size());
            constant_type typ { alloc, type_tag::pair, { constant_type { alloc, type_tag::data }, constant_type { alloc, type_tag::data } } };
            for (const auto &p: m)
                cl.emplace_back(alloc, constant_pair { alloc, constant { alloc, p->first }, constant { alloc, p->second } });
            return value::make_list(alloc, std::move(typ) , std::move(cl));
        }
        throw error(fmt::format("invalid input for un_map_data: {}!", t));
    }

    value un_list_data(allocator &alloc, const value &t) {
        if (const auto &d = t.as_data(); std::holds_alternative<data::list_type>(*d)) {
            const auto &l = std::get<data::list_type>(*d);
            constant_list::list_type cl { alloc };
            cl.reserve(l.size());
            for (const auto &d: l)
                cl.emplace_back(alloc, d);
            return value::make_list(alloc, constant_type { alloc, type_tag::data }, std::move(cl));
        }
        throw error(fmt::format("invalid input for un_list_data: {}!", t));
    }

    value un_i_data(allocator &alloc, const value &t)
    {
        if (const auto &d = t.as_data(); std::holds_alternative<data::int_type>(*d))
            return { alloc, std::get<data::int_type>(*d) };
        throw error(fmt::format("invalid input for un_i_data: {}!", t));
    }

    value un_b_data(allocator &alloc, const value &t) {
        if (const auto &d = t.as_data(); std::holds_alternative<data::bstr_type>(*d))
            return { alloc, *std::get<data::bstr_type>(*d) };
        throw error(fmt::format("invalid input for un_b_data: {}!", t));
    }

    value equals_data(allocator &alloc, const value &d1, const value &d2)
    {
        return value::boolean(alloc, d1.as_data() == d2.as_data());
    }

    value mk_pair_data(allocator &alloc, const value &fst, const value &snd)
    {
        return { alloc, constant { alloc, constant_pair { alloc, constant { alloc, fst.as_data() }, constant { alloc, snd.as_data() } } } };
    }

    value mk_nil_data(allocator &alloc, const value &)
    {
        return value::make_list(alloc, constant_type { alloc, type_tag::data });
    }

    value mk_nil_pair_data(allocator &alloc, const value &)
    {
        constant_type::list_type nested { alloc };
        nested.reserve(2);
        nested.emplace_back(alloc, type_tag::data);
        nested.emplace_back(alloc, type_tag::data);
        return value::make_list(alloc, constant_type { alloc, type_tag::pair, std::move(nested) });
    }

    value serialize_data(allocator &alloc, const value &d)
    {
        return { alloc, d.as_data().as_cbor(alloc) };
    }

    value verify_ecdsa_secp_256k1_signature(allocator &alloc, const value &vk, const value &msg, const value &sig)
    {
        return value::boolean(alloc, crypto::secp256k1::ecdsa::verify(*sig.as_bstr(), *vk.as_bstr(), *msg.as_bstr()));
    }

    value verify_schnorr_secp_256k1_signature(allocator &alloc, const value &vk, const value &msg, const value &sig)
    {
        return value::boolean(alloc, crypto::secp256k1::schnorr::verify(*sig.as_bstr(), *vk.as_bstr(), *msg.as_bstr()));
    }

    value integer_to_byte_string(allocator &alloc, const value &msb_t, const value &w_t, const value &val)
    {
        static cpp_int max_val { boost::multiprecision::pow(cpp_int { 2 }, 65536) };
        const auto msb = msb_t.as_bool();
        const auto w = static_cast<size_t>(*w_t.as_int());
        const auto &v = val.as_int();
        if (*v < 0) [[unlikely]]
            throw error(fmt::format("integer_to_byte_string requires non-negative integers but got: {}", v));
        if (*v >= max_val) [[unlikely]]
            throw error(fmt::format("integer_to_byte_string allows only values less than 2^65536 but got: {}", v));
        bstr_type::value_type::base_type bytes { alloc.resource() };
        if (*v > 0) [[likely]]
            boost::multiprecision::export_bits(*val.as_int(), std::back_inserter(bytes), 8, msb);
        if (w) {
            if (w > 8192) [[unlikely]]
                throw error(fmt::format("maximum allowed width is 8192 but got {}!", w));
            if (bytes.size() > w) [[unlikely]]
                throw error(fmt::format("expected {} bytes but got {}", bytes.size(), w));
            if (bytes.size() < w) {
                const auto orig_size = bytes.size();
                const auto padding_size = w - orig_size;
                bytes.resize(w); // fills the new elements with 0, so nothing to do in the lsb case
                if (msb) {
                    // do in reverse to not override the data before it has been copied
                    for (int64_t i = orig_size - 1; i >= 0; --i)
                        bytes[i + padding_size] = bytes[i];
                    std::fill(bytes.begin(), bytes.begin() + padding_size, 0);
                }
            }
        }
        return { alloc, bstr_type { alloc, bstr_type::value_type { alloc, std::move(bytes) } } };
    }

    value byte_string_to_integer(allocator &alloc, const value &msb_t, const value &b)
    {
        const auto msb = msb_t.as_bool();
        const auto &bytes = b.as_bstr();
        bint_type::value_type val;
        if (!bytes->empty()) [[likely]]
            boost::multiprecision::import_bits(val, bytes->begin(), bytes->end(), 8, msb);
        return { alloc, bint_type { alloc, std::move(val) } };
    }

    value bls12_381_g1_add(allocator &alloc, const value &a, const value &b)
    {
        blst_p1 out;
        blst_p1_add(&out, &a.as_bls_g1().get(), &b.as_bls_g1().get());
        return { alloc, out };
    }

    value bls12_381_g1_neg(allocator &alloc, const value &a)
    {
        blst_p1 out { a.as_bls_g1().get() };
        blst_p1_cneg(&out, true);
        return { alloc, out };
    }

    static blst_scalar bls12_381_make_scalar(const bint_type &k_t)
    {
        static const cpp_int scalar_period { "0x73eda753299d7d483339d80809a1d80553bda402fffe5bfeffffffff00000001" };
        cpp_int k { *k_t % scalar_period };
        if (k < 0)
            k += scalar_period;
        uint8_vector k_bytes {};
        boost::multiprecision::export_bits(k, std::back_inserter(k_bytes), 8, false);
        while (k_bytes.size() < 32)
            k_bytes.emplace_back(0);
        if (k_bytes.size() > 32) [[unlikely]]
            throw error(fmt::format("expected {} scalar must be not more than 32 bytes but got {}!", k_bytes.size(), k));
        blst_scalar k_s {};
        blst_scalar_from_lendian(&k_s, k_bytes.data());
        return k_s;
    }

    static blst_scalar bls12_381_make_scalar(const value &k_t)
    {
        return bls12_381_make_scalar(k_t.as_int());
    }

    value bls12_381_g1_scalar_mul(allocator &alloc, const value &k_t, const value &v_t)
    {
        blst_p1 out;
        const auto k_s = bls12_381_make_scalar(k_t);
        blst_p1_mult(&out, &v_t.as_bls_g1().get(), reinterpret_cast<const ::byte *>(&k_s), sizeof(k_s) * 8);
        return { alloc, out };
    }

    value bls12_381_g1_equal(allocator &alloc, const value &a, const value &b)
    {
        return value::boolean(alloc, blst_p1_is_equal(&a.as_bls_g1().get(), &b.as_bls_g1().get()));
    }

    value bls12_381_g1_hash_to_group(allocator &alloc, const value &msg_t, const value &dst_t)
    {
        const auto &msg = msg_t.as_bstr();
        const auto &dst = dst_t.as_bstr();
        if (dst->size() > 255) [[unlikely]]
            throw error(fmt::format("dst must be less than 256 bytes but got {}!", dst->size()));
        blst_p1 out;
        blst_hash_to_g1(&out, msg->data(), msg->size(), dst->data(), dst->size());
        return { alloc, out };
    }

    value bls12_381_g1_compress(allocator &alloc, const value &v)
    {
        return { alloc, bls_g1_compress(alloc, v.as_bls_g1()) };
    }

    value bls12_381_g1_uncompress(allocator &alloc, const value &v)
    {
        return { alloc, bls_g1_decompress(*v.as_bstr()) };
    }

    value bls12_381_g2_add(allocator &alloc, const value &a, const value &b)
    {
        blst_p2 out;
        blst_p2_add(&out, &a.as_bls_g2().get(), &b.as_bls_g2().get());
        return { alloc, out };
    }

    value bls12_381_g2_neg(allocator &alloc, const value &a)
    {
        blst_p2 out { a.as_bls_g2().get() };
        blst_p2_cneg(&out, true);
        return { alloc, out };
    }

    value bls12_381_g2_scalar_mul(allocator &alloc, const value &k_t, const value &v_t)
    {
        blst_p2 out;
        const auto k_s = bls12_381_make_scalar(k_t);
        blst_p2_mult(&out, &v_t.as_bls_g2().get(), reinterpret_cast<const ::byte *>(&k_s), sizeof(k_s) * 8);
        return { alloc, out };
    }

    value bls12_381_g2_equal(allocator &alloc, const value &a, const value &b)
    {
        return value::boolean(alloc, blst_p2_is_equal(&a.as_bls_g2().get(), &b.as_bls_g2().get()));
    }

    value bls12_381_g2_hash_to_group(allocator &alloc, const value &msg_t, const value &dst_t)
    {
        const auto &msg = msg_t.as_bstr();
        const auto &dst = dst_t.as_bstr();
        if (dst->size() > 255) [[unlikely]]
            throw error(fmt::format("dst must be less than 256 bytes but got {}!", dst->size()));
        blst_p2 out;
        blst_hash_to_g2(&out, msg->data(), msg->size(), dst->data(), dst->size());
        return { alloc, out };
    }

    value bls12_381_g2_compress(allocator &alloc, const value &v)
    {
        return { alloc, bls_g2_compress(alloc, v.as_bls_g2()) };
    }

    value bls12_381_g2_uncompress(allocator &alloc, const value &v)
    {
        return { alloc, bls_g2_decompress(*v.as_bstr()) };
    }

    value bls12_381_miller_loop(allocator &alloc, const value &g1, const value &g2)
    {
        blst_p1_affine g1_a {};
        blst_p1_to_affine(&g1_a, &g1.as_bls_g1().get());
        blst_p2_affine g2_a {};
        blst_p2_to_affine(&g2_a, &g2.as_bls_g2().get());
        blst_fp12 out;
        blst_miller_loop(&out, &g2_a, &g1_a);
        return { alloc, out };
    }

    value bls12_381_mul_ml_result(allocator &alloc, const value &a, const value &b)
    {
        blst_fp12 out;
        blst_fp12_mul(&out, &a.as_bls_ml_res().get(), &b.as_bls_ml_res().get());
        return { alloc, out };
    }

    value bls12_381_final_verify(allocator &alloc, const value &a, const value &b)
    {
        return value::boolean(alloc, blst_fp12_finalverify(&a.as_bls_ml_res().get(), &b.as_bls_ml_res().get()));
    }

    value and_byte_string(allocator &alloc, const value &extend_v, const value &a_v, const value &b_v)
    {
        const auto &a = a_v.as_bstr();
        const auto &b = b_v.as_bstr();
        const auto min_sz = std::min(a->size(), b->size());
        const auto max_sz = std::max(a->size(), b->size());
        const auto extend = extend_v.as_bool() & (min_sz < max_sz);
        bstr_type::value_type res { alloc };
        res.reserve(extend ? max_sz : min_sz);
        for (size_t i = 0; i < min_sz; i++)
            res.emplace_back((*a)[i] & (*b)[i]);
        if (extend) {
            const auto &longer = a->size() == max_sz ? a : b;
            for (size_t i = min_sz; i < max_sz; i++)
                res.emplace_back((*longer)[i]);
        }
        return { alloc, std::move(res) };
    }

    value or_byte_string(allocator &alloc, const value &extend_v, const value &a_v, const value &b_v)
    {
        const auto &a = a_v.as_bstr();
        const auto &b = b_v.as_bstr();
        const auto min_sz = std::min(a->size(), b->size());
        const auto max_sz = std::max(a->size(), b->size());
        const auto extend = extend_v.as_bool() & (min_sz < max_sz);
        bstr_type::value_type res { alloc };
        res.reserve(extend ? max_sz : min_sz);
        for (size_t i = 0; i < min_sz; i++)
            res.emplace_back((*a)[i] | (*b)[i]);
        if (extend) {
            const auto &longer = a->size() == max_sz ? a : b;
            for (size_t i = min_sz; i < max_sz; i++)
                res.emplace_back((*longer)[i]);
        }
        return { alloc, std::move(res) };
    }

    value xor_byte_string(allocator &alloc, const value &extend_v, const value &a_v, const value &b_v)
    {
        const auto &a = a_v.as_bstr();
        const auto &b = b_v.as_bstr();
        const auto min_sz = std::min(a->size(), b->size());
        const auto max_sz = std::max(a->size(), b->size());
        const auto extend = extend_v.as_bool() & (min_sz < max_sz);
        bstr_type::value_type res { alloc };
        res.reserve(extend ? max_sz : min_sz);
        for (size_t i = 0; i < min_sz; i++)
            res.emplace_back((*a)[i] ^ (*b)[i]);
        if (extend) {
            const auto &longer = a->size() == max_sz ? a : b;
            for (size_t i = min_sz; i < max_sz; i++)
                res.emplace_back((*longer)[i] ^ 0x00);
        }
        return { alloc, std::move(res) };
    }

    value complement_byte_string(allocator &alloc, const value &s_v)
    {
        const auto &s = s_v.as_bstr();
        bstr_type::value_type res { alloc };
        res.reserve(s->size());
        for (auto k: *s)
            res.emplace_back(~k);
        return { alloc, std::move(res) };
    }

    value shift_byte_string(allocator &alloc, const value &b_v, const value &n_v)
    {
        const auto &b = b_v.as_bstr();
        const auto &n = n_v.as_int();
        const int n_bits = b->size() << 3;
        bstr_type::value_type res { alloc };
        if (b->empty()) {
            return { alloc, std::move(res) };
        }
        if (*n == 0) {
            res = *b;
            return { alloc, std::move(res) };
        }
        res.reserve(b->size());
        while (res.size() < b->size())
            res.emplace_back(0);
        if (boost::multiprecision::abs(*n) < n_bits) {
            const int shift = static_cast<int>(*n);
            size_t tgt_byte = res.size() - 1;
            uint8_t tgt_mask = 0x01;
            for (int tgt_idx = 0, src_idx = tgt_idx - shift; tgt_idx < n_bits; ++tgt_idx, ++src_idx) {
                bool bit = false;
                if (src_idx >= 0 && src_idx < n_bits) {
                    const auto src_byte = b->size() - (src_idx >> 3) - 1;
                    uint8_t src_mask = 1;
                    for (auto bit_pos = src_idx & 0x7; bit_pos; --bit_pos) {
                        src_mask <<= 1;
                    }
                    bit = ((*b)[src_byte] & src_mask) > 0;
                }
                if (bit)
                    res[tgt_byte] |= tgt_mask;
                if (tgt_mask == 0x80) {
                    tgt_mask = 0x01;
                    --tgt_byte;
                } else {
                    tgt_mask <<= 1;
                }
            }
        }
        return { alloc, std::move(res) };
    }

    value rotate_byte_string(allocator &alloc, const value &b_v, const value &n_v)
    {
        const auto &b = b_v.as_bstr();
        const auto &n = n_v.as_int();
        const int n_bits = b->size() << 3;
        bstr_type::value_type res { alloc };
        if (b->empty()) {
            return { alloc, std::move(res) };
        }
        if (*n % n_bits == 0) {
            res = *b;
            return { alloc, std::move(res) };
        }
        res.reserve(b->size());
        while (res.size() < b->size()) {
            res.emplace_back(0);
        }
        const int shift = static_cast<int>(*n % n_bits);
        size_t tgt_byte = res.size() - 1;
        uint8_t tgt_mask = 0x01;
        int src_idx = -shift % n_bits;
        if (src_idx < 0)
            src_idx += n_bits;
        auto src_byte = b->size() - (src_idx >> 3) - 1;
        uint8_t src_mask = 1;
        for (auto bit_pos = src_idx & 0x7; bit_pos; --bit_pos) {
            src_mask <<= 1;
        }
        for (;;) {
            if (const bool bit = ((*b)[src_byte] & src_mask) > 0; bit)
                res[tgt_byte] |= tgt_mask;
            if (src_mask == 0x80) {
                src_mask = 0x01;
                if (src_byte == 0)
                    src_byte = b->size() - 1;
                else
                    --src_byte;
            } else {
                src_mask <<= 1;
            }
            if (tgt_mask == 0x80) {
                tgt_mask = 0x01;
                if (tgt_byte == 0)
                    break;
                --tgt_byte;
            } else {
                tgt_mask <<= 1;
            }
        }
        return { alloc, std::move(res) };
    }

    value count_set_bits(allocator &alloc, const value &b_v)
    {
        const auto &b = b_v.as_bstr();
        int cnt = 0;
        for (auto k: *b)
            cnt += std::popcount(k);
        return { alloc, bint_type { alloc, cnt } };
    }

    value find_first_set_bit(allocator &alloc, const value &b_v)
    {
        // position is counted from the right!
        const auto &b = b_v.as_bstr();
        int cnt = 0;
        for (auto k: std::ranges::views::reverse(*b)) {
            const auto k_cnt = std::countr_zero(k);
            cnt += k_cnt;
            if (k_cnt != 8)
                break;
        }
        if (cnt == static_cast<int>(b->size() << 3))
            cnt = -1;
        return { alloc, bint_type { alloc, cnt } };
    }

    value read_bit(allocator &alloc, const value &b_v, const value &pos_v)
    {
        // position is counted from the right!
        const auto &b = b_v.as_bstr();
        const auto &pos = pos_v.as_int();
        const auto n_bits = b->size() << 3;
        if (*pos < 0 || *pos >= n_bits) [[unlikely]]
            throw error(fmt::format("readBit: the bit position out of range: {}", *pos));
        // convert into the position from the left
        const auto idx = static_cast<size_t>(*pos);
        const auto byte_idx = b->size() - (idx >> 3) - 1;
        uint8_t mask = 1;
        for (auto bit_pos = idx & 0x7; bit_pos; --bit_pos) {
            mask <<= 1;
        }
        const bool res = ((*b)[byte_idx] & mask) > 0;
        return value::boolean(alloc, res);
    }

    value write_bits(allocator &alloc, const value &b_v, const value &indices_v, const value &bit_v)
    {
        const auto &b = b_v.as_bstr();
        const auto n_bits = b->size() << 3;
        const auto bit = bit_v.as_bool();
        const auto &indices = indices_v.as_list();
        bstr_type::value_type res { alloc };
        res = *b;
        indices.for_each([&](const auto &idx_v) {
            const auto &idx = idx_v.as_int();
            if (*idx < 0 || *idx >= n_bits) [[unlikely]]
                throw error(fmt::format("writeBits: the bit position out of range: {}", *idx));
            const auto pos = static_cast<size_t>(*idx);
            const auto byte_idx = b->size() - (pos >> 3) - 1;
            uint8_t mask = 1;
            for (auto bit_pos = pos & 0x7; bit_pos; --bit_pos) {
                mask <<= 1;
            }
            if (bit)
                res[byte_idx] |= mask;
            else
                res[byte_idx] &= ~mask;
        });
        return { alloc, std::move(res) };
    }

    value replicate_byte(allocator &alloc, const value &len_v, const value &b_v)
    {
        const auto &len = len_v.as_int();
        if (*len < 0 || *len > 8192) [[unlikely]]
            throw error(fmt::format("replicateByte: the length is out of range: {}", *len));
        const auto &b = b_v.as_int();
        if (*b < 0 || *b > 255) [[unlikely]]
            throw error(fmt::format("replicateByte: the byte is out of range: {}", *b));
        const auto k = static_cast<uint8_t>(*b);
        const auto sz = static_cast<size_t>(*len);
        bstr_type::value_type res { alloc };
        res.reserve(sz);
        while (res.size() < sz)
            res.emplace_back(k);
        return { alloc, std::move(res) };
    }

    value ripemd_160(allocator &alloc, const value &b)
    {
        bstr_type::value_type res { alloc, sizeof(ripemd_160::hash_t) };
        ripemd_160::digest(res, *b.as_bstr());
        return { alloc, std::move(res) };
    }

    static cpp_int modular_inverse(cpp_int a, const cpp_int &modulus)
    {
        a %= modulus;
        if (a < 0)
            a += modulus;
        cpp_int old_r = modulus;
        cpp_int r = a;
        cpp_int old_t = 0;
        cpp_int t = 1;
        while (r != 0) {
            const cpp_int quotient = old_r / r;
            cpp_int next_r = old_r - quotient * r;
            old_r = std::move(r);
            r = std::move(next_r);
            cpp_int next_t = old_t - quotient * t;
            old_t = std::move(t);
            t = std::move(next_t);
        }
        if (old_r != 1) [[unlikely]]
            throw error("the base is not invertible for expModInteger");
        old_t %= modulus;
        return old_t < 0 ? old_t + modulus : old_t;
    }

    value exp_mod_integer(allocator &alloc, const value &a_v, const value &e_v, const value &m_v)
    {
        const auto &a = a_v.as_int();
        const auto &e = e_v.as_int();
        const auto &m = m_v.as_int();
        static const cpp_int min_integer = -(cpp_int { 1 } << 8191);
        static const cpp_int max_integer = (cpp_int { 1 } << 8191) - 1;
        if (*m <= 0 || *m > max_integer) [[unlikely]]
            throw error(fmt::format("invalid modulus for expModInteger: {}", m));
        if (*m == 1)
            return { alloc, 0 };
        if (*a == 0 && *e < 0) [[unlikely]]
            throw error("zero is not invertible for expModInteger");
        if (*a < min_integer || *a > max_integer || *e < min_integer || *e > max_integer) [[unlikely]]
            throw error("an expModInteger argument is out of bounds");

        cpp_int base = *a % *m;
        if (base < 0)
            base += *m;
        cpp_int exponent = *e;
        if (exponent < 0) {
            base = modular_inverse(std::move(base), *m);
            exponent = -exponent;
        }
        return { alloc, boost::multiprecision::powm(base, exponent, *m) };
    }

    value drop_list(allocator &alloc, const value &count_v, const value &list_v)
    {
        const auto &src = list_v.as_list();
        const auto &count = *count_v.as_int();
        if (count <= 0)
            return list_v;
        if (count >= src.size())
            return { alloc, constant { alloc, src.drop(alloc, src.size()) } };
        return { alloc, constant { alloc, src.drop(alloc, count.convert_to<size_t>()) } };
    }

    value length_of_array(allocator &alloc, const value &array_v)
    {
        return { alloc, numeric_cast<int64_t>(array_v.as_array().size()) };
    }

    value list_to_array(allocator &alloc, const value &list_v)
    {
        const auto &src = list_v.as_list();
        constant_list::list_type vals { alloc };
        src.copy_to(vals);
        return { alloc, constant { alloc, constant_array {
            alloc, constant_type { src.typ() }, std::move(vals)
        } } };
    }

    value index_array(allocator &alloc, const value &array_v, const value &index_v)
    {
        const auto &src = array_v.as_array();
        const auto &index = *index_v.as_int();
        if (index < 0 || index >= src.size()) [[unlikely]]
            throw error(fmt::format("array index {} is out of bounds for an array of size {}",
                index_v.as_int(), src.size()));
        return { alloc, src.at(index.convert_to<size_t>()) };
    }

    template<typename Point, typename Generator, typename Multiply, typename Add, typename Extract>
    static Point bls12_381_multi_scalar_mul(const constant_list &scalars, const constant_list &points,
            Generator generator, Multiply multiply, Add add, Extract extract)
    {
        static const cpp_int lower = -(cpp_int { 1 } << 4095);
        static const cpp_int upper = (cpp_int { 1 } << 4095) - 1;
        for (const auto &scalar: scalars) {
            const auto &i = *scalar.as_int();
            if (i < lower || i > upper) [[unlikely]]
                throw error("a multiScalarMul scalar exceeds the 512-byte bound");
        }

        const blst_scalar zero {};
        Point result {};
        multiply(&result, generator(), reinterpret_cast<const ::byte *>(&zero), sizeof(zero) * 8);
        auto scalar_it = scalars.begin();
        auto point_it = points.begin();
        for (; scalar_it != scalars.end() && point_it != points.end(); ++scalar_it, ++point_it) {
            const auto scalar = bls12_381_make_scalar(scalar_it->as_int());
            Point term {};
            multiply(&term, &extract(*point_it), reinterpret_cast<const ::byte *>(&scalar), sizeof(scalar) * 8);
            Point sum {};
            add(&sum, &result, &term);
            result = sum;
        }
        return result;
    }

    value bls12_381_g1_multi_scalar_mul(allocator &alloc, const value &scalars_v, const value &points_v)
    {
        // BLST defines these entry points with internal point type names. Keep the calls direct:
        // UBSan rejects indirect calls through the nominally different public-header signatures.
        const auto point = bls12_381_multi_scalar_mul<blst_p1>(scalars_v.as_list(), points_v.as_list(),
            [] { return blst_p1_generator(); },
            [](blst_p1 *out, const blst_p1 *point, const ::byte *scalar, const size_t num_bits) {
                blst_p1_mult(out, point, scalar, num_bits);
            },
            [](blst_p1 *out, const blst_p1 *a, const blst_p1 *b) { blst_p1_add(out, a, b); },
            [](const constant &c) -> const blst_p1 & { return std::get<bls12_381_g1_element>(*c).get(); });
        return { alloc, point };
    }

    value bls12_381_g2_multi_scalar_mul(allocator &alloc, const value &scalars_v, const value &points_v)
    {
        const auto point = bls12_381_multi_scalar_mul<blst_p2>(scalars_v.as_list(), points_v.as_list(),
            [] { return blst_p2_generator(); },
            [](blst_p2 *out, const blst_p2 *point, const ::byte *scalar, const size_t num_bits) {
                blst_p2_mult(out, point, scalar, num_bits);
            },
            [](blst_p2 *out, const blst_p2 *a, const blst_p2 *b) { blst_p2_add(out, a, b); },
            [](const constant &c) -> const blst_p2 & { return std::get<bls12_381_g2_element>(*c).get(); });
        return { alloc, point };
    }

    static const cpp_int value_min_quantity = -(cpp_int { 1 } << 127);
    static const cpp_int value_max_quantity = (cpp_int { 1 } << 127) - 1;

    static asset_value::key_type value_key(const bstr_type &key)
    {
        return { key->begin(), key->end() };
    }

    static void check_value_quantity(const cpp_int &quantity)
    {
        if (quantity < value_min_quantity || quantity > value_max_quantity) [[unlikely]]
            throw error("Value quantity is outside the signed 128-bit range");
    }

    static value make_asset_value(allocator &alloc, asset_value::map_type &&map)
    {
        return { alloc, constant { alloc, asset_value { alloc, std::move(map) } } };
    }

    value insert_coin(allocator &alloc, const value &currency_v, const value &token_v,
            const value &quantity_v, const value &value_v)
    {
        auto map = *value_v.as_asset_value();
        const auto currency = value_key(currency_v.as_bstr());
        const auto token = value_key(token_v.as_bstr());
        const auto &quantity = *quantity_v.as_int();
        if (quantity == 0) {
            if (auto currency_it = map.find(currency); currency_it != map.end()) {
                currency_it->second.erase(token);
                if (currency_it->second.empty())
                    map.erase(currency_it);
            }
            return make_asset_value(alloc, std::move(map));
        }
        if (currency.size() > asset_value::max_key_size || token.size() > asset_value::max_key_size) [[unlikely]]
            throw error("insertCoin key exceeds 32 bytes");
        check_value_quantity(quantity);
        map[currency][token] = quantity;
        return make_asset_value(alloc, std::move(map));
    }

    value lookup_coin(allocator &alloc, const value &currency_v, const value &token_v, const value &value_v)
    {
        const auto &map = *value_v.as_asset_value();
        const auto currency = value_key(currency_v.as_bstr());
        const auto token = value_key(token_v.as_bstr());
        if (const auto currency_it = map.find(currency); currency_it != map.end()) {
            if (const auto token_it = currency_it->second.find(token); token_it != currency_it->second.end())
                return { alloc, token_it->second };
        }
        return { alloc, 0 };
    }

    value union_value(allocator &alloc, const value &a_v, const value &b_v)
    {
        auto map = *a_v.as_asset_value();
        for (const auto &[currency, tokens]: *b_v.as_asset_value()) {
            auto &dst = map[currency];
            for (const auto &[token, quantity]: tokens) {
                auto &sum = dst[token];
                sum += quantity;
                check_value_quantity(sum);
                if (sum == 0)
                    dst.erase(token);
            }
            if (dst.empty())
                map.erase(currency);
        }
        return make_asset_value(alloc, std::move(map));
    }

    value value_contains(allocator &alloc, const value &container_v, const value &contained_v)
    {
        const auto &container = container_v.as_asset_value();
        const auto &contained = contained_v.as_asset_value();
        if (container.negative_amounts() || contained.negative_amounts()) [[unlikely]]
            throw error("valueContains does not accept negative quantities");
        if (container.total_size() < contained.total_size())
            return value::boolean(alloc, false);
        for (const auto &[currency, tokens]: *contained) {
            const auto currency_it = container->find(currency);
            if (currency_it == container->end())
                return value::boolean(alloc, false);
            for (const auto &[token, quantity]: tokens) {
                const auto token_it = currency_it->second.find(token);
                if (token_it == currency_it->second.end() || token_it->second < quantity)
                    return value::boolean(alloc, false);
            }
        }
        return value::boolean(alloc, true);
    }

    value value_data(allocator &alloc, const value &value_v)
    {
        const auto &v = value_v.as_asset_value();
        if (v.total_size() > asset_value::max_data_size) [[unlikely]]
            throw error("valueData input exceeds 40000 entries");
        data::map_type currencies { alloc };
        currencies.reserve(v->size());
        for (const auto &[currency, tokens]: *v) {
            data::map_type token_data { alloc };
            token_data.reserve(tokens.size());
            for (const auto &[token, quantity]: tokens) {
                token_data.emplace_back(alloc,
                    data::bstr(alloc, buffer { token.data(), token.size() }), data::bint(alloc, quantity));
            }
            currencies.emplace_back(alloc,
                data::bstr(alloc, buffer { currency.data(), currency.size() }), data::map(alloc, std::move(token_data)));
        }
        return { alloc, data::map(alloc, std::move(currencies)) };
    }

    value un_value_data(allocator &alloc, const value &data_v)
    {
        asset_value::input_type entries {};
        const auto &outer_data = data_v.as_data();
        if (!std::holds_alternative<data::map_type>(*outer_data)) [[unlikely]]
            throw error("unValueData expects a map");
        for (const auto &currency_pair: std::get<data::map_type>(*outer_data)) {
            if (!std::holds_alternative<data::bstr_type>(*currency_pair->first)
                    || !std::holds_alternative<data::map_type>(*currency_pair->second)) [[unlikely]]
                throw error("unValueData has an invalid currency entry");
            const auto &currency_raw = std::get<data::bstr_type>(*currency_pair->first);
            asset_value::key_type currency { currency_raw->begin(), currency_raw->end() };
            asset_value::input_inner_type tokens {};
            for (const auto &token_pair: std::get<data::map_type>(*currency_pair->second)) {
                if (!std::holds_alternative<data::bstr_type>(*token_pair->first)
                        || !std::holds_alternative<data::int_type>(*token_pair->second)) [[unlikely]]
                    throw error("unValueData has an invalid token entry");
                const auto &token_raw = std::get<data::bstr_type>(*token_pair->first);
                const auto &quantity = std::get<data::int_type>(*token_pair->second);
                tokens.emplace_back(asset_value::key_type { token_raw->begin(), token_raw->end() }, *quantity);
            }
            entries.emplace_back(std::move(currency), std::move(tokens));
        }
        return { alloc, constant { alloc, asset_value::from_list(alloc, std::move(entries)) } };
    }

    value scale_value(allocator &alloc, const value &factor_v, const value &value_v)
    {
        const auto &factor = *factor_v.as_int();
        if (factor == 0)
            return make_asset_value(alloc, asset_value::map_type {});
        auto map = *value_v.as_asset_value();
        for (auto &[currency, tokens]: map) {
            static_cast<void>(currency);
            for (auto &[token, quantity]: tokens) {
                static_cast<void>(token);
                quantity *= factor;
                check_value_quantity(quantity);
            }
        }
        return make_asset_value(alloc, std::move(map));
    }

    const std::array<builtin_descriptor, builtin_tag_count> descriptors = [] {
        std::array<builtin_descriptor, builtin_tag_count> descriptors {};
#define TURBO_PLUTUS_BUILTIN(tag, arity, function, name, polymorphic, batch) \
        descriptors[static_cast<size_t>(builtin_tag::tag)] = { arity, polymorphic, batch, name };
#include <turbo/plutus/builtin-registry.inc>
#undef TURBO_PLUTUS_BUILTIN
        return descriptors;
    }();

    builtin_semantics semantics_variant(const cardano::script_type typ, const uint64_t protocol_major)
    {
        using cardano::script_type;
        if (protocol_major >= 11) {
            switch (typ) {
                case script_type::plutus_v1:
                case script_type::plutus_v2:
                    return builtin_semantics::d;
                case script_type::plutus_v3:
                    return builtin_semantics::e;
                [[unlikely]] default: break;
            }
        } else {
            switch (typ) {
                case script_type::plutus_v1:
                case script_type::plutus_v2:
                    return protocol_major < 9 ? builtin_semantics::a : builtin_semantics::b;
                case script_type::plutus_v3:
                    return builtin_semantics::c;
                [[unlikely]] default: break;
            }
        }
        throw error(fmt::format("unsupported script type: {}", typ));
    }

    static bool _ledger_language_available(const cardano::script_type typ, const uint64_t protocol_major)
    {
        using cardano::script_type;
        switch (typ) {
            case script_type::plutus_v1: return protocol_major >= 5;
            case script_type::plutus_v2: return protocol_major >= 7;
            case script_type::plutus_v3: return protocol_major >= 9;
            default: return false;
        }
    }

    bool available(const builtin_tag tag, const cardano::script_type typ, const uint64_t protocol_major)
    {
        if (!_ledger_language_available(typ, protocol_major))
            return false;
        const auto batch = descriptor(tag).batch;
        using cardano::script_type;
        switch (typ) {
            case script_type::plutus_v1:
                return protocol_major >= 11 || batch == 1;
            case script_type::plutus_v2:
                if (protocol_major >= 11)
                    return true;
                if (protocol_major >= 10
                        && (tag == builtin_tag::integer_to_byte_string || tag == builtin_tag::byte_string_to_integer))
                    return true;
                if (protocol_major >= 8)
                    return batch <= 3;
                return batch <= 2;
            case script_type::plutus_v3:
                if (protocol_major >= 11)
                    return true;
                return batch <= (protocol_major >= 10 ? 5 : 4);
            default: return false;
        }
    }

    bool version_available(const version &ver, const cardano::script_type typ, const uint64_t protocol_major)
    {
        if (!_ledger_language_available(typ, protocol_major))
            return false;
        if (ver == version { 1, 0, 0 })
            return true;
        if (!(ver == version { 1, 1, 0 }))
            return false;
        return typ == cardano::script_type::plutus_v3 || protocol_major >= 11;
    }

}
