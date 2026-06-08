/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cbor/encoder.hpp>
#include <turbo/plutus/flat-encoder.hpp>

namespace turbo::plutus::flat {
    struct encoder {
        void var_uint(const cpp_int &u)
        {
            if (u < 0) [[unlikely]]
                throw error("var_uint can encode only non-negative values!");
            uint8_vector tmp {};
            boost::multiprecision::export_bits(u, std::back_inserter(tmp), 7, false);
            for (size_t j = 0; j < tmp.size(); ++j)
                put_byte((j + 1 < tmp.size() ? 0x80 : 0) | tmp[j]);
        }

        void fixed_uint(const size_t num_bits, const uint64_t v)
        {
            if (num_bits == 0) [[unlikely]]
                throw error("the number of bits must be greater than 0!");
            uint64_t mask = 1ULL << (num_bits - 1);
            for (size_t i = 0; i < num_bits; ++i) {
                put_bit(v & mask);
                mask >>= 1;
            }
        }

        void bytestring(const buffer b)
        {
            put_padding();
            for (size_t pos = 0; pos < b.size(); pos += 255) {
                const auto chunk_size = std::min(b.size() - pos, size_t { 255 });
                put_byte(chunk_size);
                for (size_t i = pos, end = pos + chunk_size; i < end; ++i)
                    _bytes << b[i];
            }
            put_byte(0);
        }

        void encode(const version &v)
        {
            var_uint(v.major);
            var_uint(v.minor);
            var_uint(v.patch);
        }

        void encode(const term_tag tag)
        {
            fixed_uint(4, static_cast<uint64_t>(tag));
        }

        void encode(const type_tag tag)
        {
            fixed_uint(4, static_cast<uint64_t>(tag));
        }

        void encode(const builtin_tag tag)
        {
            fixed_uint(7, static_cast<uint64_t>(tag));
        }

        void encode(const variable &v)
        {
            encode(term_tag::variable);
            if (v.idx > _num_vars) [[unlikely]]
                throw error("variable index out of range!");
            // De Bruijn indices are 1-based!
            const auto rel_idx = _num_vars - v.idx;
            var_uint(rel_idx);
        }

        void encode(const t_delay &d)
        {
            encode(term_tag::delay);
            encode(d.expr);
        }

        void encode(const force &f)
        {
            encode(term_tag::force);
            encode(f.expr);
        }

        void encode(const t_lambda &v)
        {
            encode(term_tag::lambda);
            ++_num_vars;
            encode(v.expr);
            --_num_vars;
        }

        void encode(const apply &a)
        {
            encode(term_tag::apply);
            encode(a.func);
            encode(a.arg);
        }

        void encode_type(const bint_type &)
        {
            encode(type_tag::integer);
        }

        void encode_type(const bstr_type &)
        {
            encode(type_tag::bytestring);
        }

        void encode_type(const str_type &)
        {
            encode(type_tag::string);
        }

        void encode_type(const bool)
        {
            encode(type_tag::boolean);
        }

        void encode_type(const constant_type &t)
        {
            switch (t->typ) {
                case type_tag::pair: {
                    encode(type_tag::application);
                    put_bit(true);
                    encode(type_tag::application);
                    put_bit(true);
                    encode(type_tag::pair);
                    for (const auto &tt: t->nested) {
                        put_bit(true);
                        encode_type(tt);
                    }
                    break;
                }
                case type_tag::list: {
                    encode(type_tag::application);
                    put_bit(true);
                    encode(type_tag::list);
                    for (const auto &tt: t->nested) {
                        put_bit(true);
                        encode_type(tt);
                    }
                    break;
                }
                default:
                    encode(t->typ);
                    break;
            }
        }

        void encode_type(const constant_list &l)
        {
            encode(type_tag::application);
            put_bit(true);
            encode(type_tag::list);
            put_bit(true);
            encode_type(l->typ);
        }

        void encode_type(const constant_pair &p)
        {
            encode(type_tag::application);
            put_bit(true);
            encode(type_tag::application);
            put_bit(true);
            encode(type_tag::pair);
            put_bit(true);
            std::visit([&](const auto &v) {
                encode_type(v);
            }, *p->first);
            put_bit(true);
            std::visit([&](const auto &v) {
                encode_type(v);
            }, *p->second);
        }

        void encode_type(const data &)
        {
            encode(type_tag::data);
        }

        void encode_type(const bls12_381_g1_element &)
        {
            encode(type_tag::bls12_381_g1_element);
        }

        void encode_type(const bls12_381_g2_element &)
        {
            encode(type_tag::bls12_381_g2_element);
        }

        void encode_type(const bls12_381_ml_result &)
        {
            encode(type_tag::bls12_381_ml_result);
        }

        void encode_type(const std::monostate)
        {
            encode(type_tag::unit);
        }

        void encode_val(const bint_type &i)
        {
            cpp_int u;
            if (*i >= 0) {
                u = *i << 1;
            } else {
                u = *i;
                u += 1;
                u = boost::multiprecision::abs(u);
                u = u << 1;
                u |= 1;
            }
            var_uint(u);
        }

        void encode_val(const bstr_type &b)
        {
            bytestring(*b);
        }

        void encode_val(const str_type &s)
        {
            bytestring(buffer { *s });
        }

        void encode_val(const bool b)
        {
            put_bit(b);
        }

        void encode_val(const constant_list &l)
        {
            for (const auto &v: l->vals) {
                put_bit(true);
                std::visit([&](const auto &vv) {
                    encode_val(vv);
                }, *v);
            }
            put_bit(false);
        }

        void encode_val(const constant_pair &p)
        {
            std::visit([&](const auto &vv) {
                encode_val(vv);
            }, *p->first);
            std::visit([&](const auto &vv) {
                encode_val(vv);
            }, *p->second);
        }

        void encode_val(const data &d)
        {
            cbor::encoder enc {};
            d.to_cbor(enc);
            bytestring(enc.cbor());
        }

        void encode_val(const bls12_381_g1_element &v)
        {
            byte_array<48> comp {};
            blst_p1_compress(reinterpret_cast<byte *>(comp.data()), &v.val);
            bytestring(comp);
        }

        void encode_val(const bls12_381_g2_element &v)
        {
            byte_array<96> comp {};
            blst_p2_compress(reinterpret_cast<byte *>(comp.data()), &v.val);
            bytestring(comp);
        }

        void encode_val(const bls12_381_ml_result &)
        {
            throw error("bls12_381_ml_result should not be serialized!");
        }

        void encode_val(const std::monostate)
        {
            // do nothing
        }

        void encode(const bint_type &i)
        {
            put_bit(true);
            encode_type(i);
            put_bit(false);
            encode_val(i);
        }

        void encode(const bstr_type &b)
        {
            put_bit(true);
            encode_type(b);
            put_bit(false);
            encode_val(b);
        }

        void encode(const str_type &s)
        {
            put_bit(true);
            encode_type(s);
            put_bit(false);
            encode_val(s);
        }

        void encode(const bool b)
        {
            put_bit(true);
            encode_type(b);
            put_bit(false);
            encode_val(b);
        }

        void encode(const constant_list &l)
        {
            put_bit(true);
            encode_type(l);
            put_bit(false);
            encode_val(l);
        }

        void encode(const constant_pair &p)
        {
            put_bit(true);
            encode_type(p);
            put_bit(false);
            encode_val(p);
        }

        void encode(const data &d)
        {
            put_bit(true);
            encode_type(d);
            put_bit(false);
            encode_val(d);
        }

        void encode(const bls12_381_g1_element &v)
        {
            put_bit(true);
            encode_type(v);
            put_bit(false);
            encode_val(v);
        }

        void encode(const bls12_381_g2_element &v)
        {
            put_bit(true);
            encode_type(v);
            put_bit(false);
            encode_val(v);
        }

        void encode(const bls12_381_ml_result &v)
        {
            put_bit(true);
            encode_type(v);
            put_bit(false);
            encode_val(v);
        }

        void encode(const std::monostate v)
        {
            put_bit(true);
            encode_type(v);
            put_bit(false);
            encode_val(v);
        }

        void encode(const constant &c)
        {
            encode(term_tag::constant);
            std::visit([&](const auto &v) {
                encode(v);
            }, *c);
        }

        void encode(const failure &)
        {
            encode(term_tag::error);
        }

        void encode(const t_builtin &b)
        {
            encode(term_tag::builtin);
            encode(b.tag);
        }

        void encode(const t_constr &c)
        {
            encode(term_tag::constr);
            var_uint(c.tag);
            for (const auto &t: *c.args) {
                put_bit(true);
                encode(t);
            }
            put_bit(false);
        }

        void encode(const t_case &c)
        {
            encode(term_tag::acase);
            encode(c.arg);
            for (const auto &t: *c.cases) {
                put_bit(true);
                encode(t);
            }
            put_bit(false);
        }

        void encode(const term &t)
        {
            std::visit([&](const auto &v) {
                encode(v);
            }, *t);
        }

        void put_bit(const bool bit)
        {
            _next_byte |= bit * _next_bit_mask;
            _next_bit_mask >>= 1;
            if (!_next_bit_mask) {
                _bytes << _next_byte;
                _next_byte = 0;
                _next_bit_mask = 0x80;
            }
        }

        void put_padding()
        {
            while (_next_bit_mask > 1)
                put_bit(false);
            put_bit(true);
        }

        void put_byte(const uint8_t byte)
        {
            size_t mask = 0x80;
            for (size_t i = 0; i < 8; ++i, mask >>= 1) {
                put_bit(byte & mask);
            }
        }

        uint8_vector &bytes()
        {
            if (_next_bit_mask != 0x80) [[unlikely]]
                throw error("bytes() called on an unpadded output!");
            return _bytes;
        }
    private:
        uint8_vector _bytes {};
        size_t _num_vars = 0;
        uint8_t _next_byte = 0;
        uint8_t _next_bit_mask = 0x80;
    };

    uint8_vector encode(const term &s)
    {
        encoder enc {};
        enc.encode(s);
        enc.put_padding();
        return std::move(enc.bytes());
    }

    uint8_vector encode(const version &v, const term &t)
    {
        encoder enc {};
        enc.encode(v);
        enc.encode(t);
        enc.put_padding();
        return std::move(enc.bytes());
    }

    uint8_vector encode_cbor(const version &v, const term &t)
    {
        cbor::encoder enc {};
        enc.bytes(encode(v, t));
        return std::move(enc.cbor());
    }
}
