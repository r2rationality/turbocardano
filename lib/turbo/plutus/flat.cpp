/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cardano/common/types.hpp>
#include <turbo/cbor/zero2.hpp>
#include <turbo/crypto/blake2b.hpp>
#include <turbo/plutus/builtins.hpp>
#include <turbo/plutus/flat.hpp>
#include <turbo/util.hpp>

namespace turbo::plutus::flat {
    struct script::impl {
        impl(allocator &alloc, uint8_vector &&bytes, const bool cbor):
            _alloc { alloc }, _bytes_raw { std::move(bytes) },
            _bytes { cbor ? _extract_cbor_data(_bytes_raw) : static_cast<buffer>(_bytes_raw) },
            _ver { _decode_version() },
            _term { _decode_term() }
        {
            if (_bytes.empty()) [[unlikely]]
                throw error("a flat script cannot be empty!");
        }

        plutus::version version() const
        {
            return _ver;
        }

        term program() const
        {
            return _term;
        }
    private:
        static constexpr size_t max_script_size = 1 << 16;
        static constexpr size_t max_varint_bits = big_int_max_size * 2 * 8;
        static constexpr size_t max_varint_bytes = max_varint_bits / 7;

        allocator &_alloc;
        uint8_vector _bytes_raw;
        buffer _bytes { _bytes_raw };
        const uint8_t *_byte_next = _bytes.data();
        const uint8_t *const _byte_end = _bytes.data() + _bytes.size();
        uint8_t _next_bit_mask = 0x80;
        size_t _num_vars = 0;
        plutus::version _ver;
        term _term;

        static buffer _extract_cbor_data(const buffer bytes)
        {
            auto cbor_item = cbor::zero2::parse(bytes);
            const auto buf = cbor_item.get().bytes();
            if (buf.size() <= max_script_size) [[likely]]
                return buf;
            throw error(fmt::format("script size of {} bytes exceeds the maximum allowed size of {}", buf.size(), max_script_size));
        }

        bool _next_bit()
        {
            if (_byte_next >= _byte_end) [[unlikely]]
                throw error(fmt::format("out of data at byte {}", _byte_pos()));
            // return a 0 or 1 so that the result can be used in a multiplication
            const bool res = (*_byte_next & _next_bit_mask) > 0;
            _next_bit_mask >>= 1;
            if (!_next_bit_mask) {
                _next_bit_mask = 0x80;
                ++_byte_next;
            }
            return res;
        }

        template<size_t NUM_BITS>
        uint8_t _decode_fixed_uint()
        {
            static_assert(NUM_BITS <= 8);
            uint8_t next_bit_mask = 1ULL << (NUM_BITS - 1);
            uint8_t val {};
            while (next_bit_mask) {
                val |= next_bit_mask * _next_bit();
                next_bit_mask >>= 1;
            }
            return val;
        }

        uint8_t _next_byte()
        {
            if (_next_bit_mask == 0x80) {
                if (_byte_next >= _byte_end) [[unlikely]]
                    throw error(fmt::format("out of data at byte {}", _byte_pos()));
                return *_byte_next++;
            }
            uint8_t res = 0;
            res += _next_bit() * 0x80;
            res += _next_bit() * 0x40;
            res += _next_bit() * 0x20;
            res += _next_bit() * 0x10;
            res += _next_bit() * 0x08;
            res += _next_bit() * 0x04;
            res += _next_bit() * 0x02;
            res += _next_bit() * 0x01;
            return res;
        }

        ptrdiff_t _byte_pos() const
        {
            return _byte_next - _bytes_raw.data();
        }

        const cpp_int &_decode_varlen_uint()
        {
            thread_local uint8_vector bytes {};
            thread_local cpp_int v {};
            bytes.clear();
            for (;;) {
                const auto b = _next_byte();
                bytes.emplace_back(b);
                if (!(b & 0x80))
                    break;
                if (bytes.size() >= max_varint_bytes) [[unlikely]]
                    throw error(fmt::format("a variable length uint that has more than {} bytes at byte: {}", max_varint_bytes, _byte_pos()));
            }
            boost::multiprecision::import_bits(v, bytes.data(), bytes.data() + bytes.size(), 7, false);
            return v;
        }

        bint_type _decode_integer()
        {
            const auto &u = _decode_varlen_uint();
            cpp_int i = u >> 1;
            if (u & 1) {
                i = -(i + 1);
            }
            return { _alloc, std::move(i) };
        }

        bool _decode_boolean()
        {
            return _next_bit();
        }

        void _decode_list(const std::function<void()> &observer)
        {
            for (;;) {
                if (!_next_bit())
                    break;
                observer();
            }
        }

        void _consume_padding()
        {
            while (!_next_bit()) {
                // do nothing
            }
            if (_next_bit_mask != 0x80) [[unlikely]]
                throw error(fmt::format("consume_padding: didn't finish on a byte boundary at bit {}!", _byte_pos()));
        }

        buffer _decode_bytestring()
        {
            thread_local uint8_vector bytes {};
            _consume_padding();
            bytes.clear();
            for (;;) {
                const size_t chunk_size = _decode_fixed_uint<8>();
                if (!chunk_size)
                    break;
                const size_t data_idx = bytes.size();
                bytes.resize(bytes.size() + chunk_size);
                if (_byte_next + chunk_size >= _byte_end)
                    throw error(fmt::format("insufficient data for a bytestring of size {} at byte: {}", chunk_size, _byte_pos()));
                memcpy(bytes.data() + data_idx, _byte_next, chunk_size);
                _byte_next += chunk_size;
            }
            return bytes;
        }

        str_type _decode_string()
        {
            auto bytes = _decode_bytestring();
            return { _alloc, std::string_view { reinterpret_cast<const char *>(bytes.data()), bytes.size() } };
        }

        data _decode_data()
        {
            const auto bytes = _decode_bytestring();
            return data::from_cbor(_alloc, bytes);
        }

        bls12_381_g1_element _decode_bls_g1()
        {
            return bls_g1_decompress(_decode_bytestring());
        }

        bls12_381_g2_element _decode_bls_g2()
        {
            return bls_g2_decompress(_decode_bytestring());
        }

        constant_type _decode_type_application(std::vector<type_tag>::iterator &it, const std::vector<type_tag>::iterator &end)
        {
            if (++it == end)
                throw error("type list too short!");
            switch (*it) {
                case type_tag::list: {
                    if (++it == end)
                        throw error("type list too short!");
                    constant_type::list_type nested { _alloc };
                    nested.emplace_back(_decode_constant_type(it, end));
                    return { _alloc, type_tag::list, { std::move(nested) } };
                }
                case type_tag::pair: {
                    if (++it == end)
                        throw error("type list too short!");
                    constant_type::list_type nested { _alloc };
                    nested.emplace_back(_decode_constant_type(it, end));
                    if (++it == end)
                        throw error("type list too short!");
                    nested.emplace_back(_decode_constant_type(it, end));
                    return { _alloc, type_tag::pair, { std::move(nested) } };
                }
                case type_tag::application:
                    return _decode_type_application(it, end);
                default:
                    throw error(fmt::format("unsupported container type for an application: {}", *it));
            }
        }

        constant_type _decode_constant_type(std::vector<type_tag>::iterator &it, const std::vector<type_tag>::iterator &end)
        {
            const auto &typ = *it;
            switch (typ) {
                case type_tag::integer:
                case type_tag::bytestring:
                case type_tag::string:
                case type_tag::unit:
                case type_tag::boolean:
                case type_tag::data:
                case type_tag::bls12_381_g1_element:
                case type_tag::bls12_381_g2_element:
                    return { _alloc, typ };
                case type_tag::list:
                case type_tag::pair:
                    throw error("list and pair types are supported only within a type application");
                case type_tag::application:
                    return _decode_type_application(it, end);
                default: throw error(fmt::format("unsupported constant type: {}", static_cast<int>(typ)));
            }
        }

        constant _decode_constant_val(const constant_type &typ)
        {
            switch (typ->typ) {
                case type_tag::integer: return { _alloc, _decode_integer() };
                case type_tag::bytestring: return { _alloc, bstr_type { _alloc, _decode_bytestring() } };
                case type_tag::string: return { _alloc, _decode_string() };
                case type_tag::unit: return { _alloc, std::monostate{} };
                case type_tag::boolean: return { _alloc, _decode_boolean() };
                case type_tag::data: return { _alloc, _decode_data() };
                case type_tag::bls12_381_g1_element: return { _alloc, _decode_bls_g1() };
                case type_tag::bls12_381_g2_element: return { _alloc, _decode_bls_g2() };
                case type_tag::list: {
                    constant_list::list_type cl { _alloc };
                    _decode_list([&] {
                        cl.emplace_back(_decode_constant_val(typ->nested.front()));
                    });
                    if (typ->nested.size() != 1) [[unlikely]]
                        throw error(fmt::format("the nested type list for a list must have just one element but has {}", typ->nested.size()));
                    return { _alloc, constant_list { _alloc, constant_type { typ->nested.front() }, std::move(cl) } };
                }
                case type_tag::pair: {
                    if (typ->nested.size() != 2) [[unlikely]]
                        throw error(fmt::format("the nested type list for a pair must have two elements but has {}", typ->nested.size()));
                    auto fst = _decode_constant_val(typ->nested.front());
                    auto snd = _decode_constant_val(typ->nested.back());
                    return { _alloc, constant_pair { _alloc, std::move(fst), std::move(snd) } };
                }
                default: throw error(fmt::format("unsupported constant type: {}", static_cast<int>(typ->typ)));
            }
        }

        constant _decode_constant()
        {
            thread_local std::vector<type_tag> types {};
            types.clear();
            for (;;) {
                if (!_next_bit())
                    break;
                types.emplace_back(static_cast<type_tag>(_decode_fixed_uint<4>()));
            }
            if (types.empty())
                throw error(fmt::format("no type is defined at byte: {}!", _byte_pos()));
            auto types_it = types.begin();
            auto typ = _decode_constant_type(types_it, types.end());
            return _decode_constant_val(std::move(typ));
        }

        t_builtin _decode_builtin()
        {
            const auto tag = static_cast<builtin_tag>(_decode_fixed_uint<7>());
            if (builtins::semantics_v2().contains(tag)) [[likely]]
                return { tag };
            throw error(fmt::format("unsupported builtin: {}!", static_cast<int>(tag)));
        }

        variable _decode_variable()
        {
            // De Bruijn indices are 1-based!
            const auto rel_idx = static_cast<size_t>(_decode_varlen_uint());
            if (rel_idx <= _num_vars) [[likely]]
                return { _num_vars - rel_idx };
            throw turbo::error(fmt::format("De Bruijn index is out of range: {} num_vars: {}", rel_idx, _num_vars));
        }

        t_delay _decode_delay()
        {
            return { _decode_term() };
        }

        t_lambda _decode_lambda()
        {
            const auto var_idx = _num_vars++;
            auto body = _decode_term();
            --_num_vars;
            return { var_idx, std::move(body) };
        }

        apply _decode_apply()
        {
            auto fun = _decode_term();
            auto arg = _decode_term();
            return { std::move(fun), std::move(arg) };
        }

        force _decode_force()
        {
            return { _decode_term() };
        }

        failure _decode_error()
        {
            return {};
        }

        t_constr _decode_constr()
        {
            const auto tag_bi = _decode_varlen_uint();
            if (tag_bi && boost::multiprecision::msb(tag_bi) >= 64) [[unlikely]]
                throw error(fmt::format("constr tag must fit into a 64-vit integer but got: {}", tag_bi));
            const auto tag = static_cast<uint64_t>(tag_bi);
            term_list::value_type args { _alloc };
            while (_next_bit()) {
                args.emplace_back(_decode_term());
            }
            return { tag, { _alloc, std::move(args) } };
        }

        t_case _decode_case()
        {
            const auto arg = _decode_term();
            term_list::value_type cases { _alloc };
            while (_next_bit()) {
                cases.emplace_back(_decode_term());
            }
            return { arg, { _alloc, std::move(cases) } };
        }

        term _decode_term()
        {
            const auto typ = static_cast<term_tag>(_decode_fixed_uint<4>());
            switch (typ) {
                case term_tag::variable: return { _alloc, _decode_variable() };
                case term_tag::delay: return { _alloc, _decode_delay() };
                case term_tag::lambda: return { _alloc, _decode_lambda() };
                case term_tag::apply: return { _alloc, _decode_apply() };
                case term_tag::constant: return { _alloc, _decode_constant() };
                case term_tag::force: return { _alloc, _decode_force() };
                case term_tag::error: return { _alloc, _decode_error() };
                case term_tag::builtin: return { _alloc, _decode_builtin() };
                case term_tag::constr:  return { _alloc, _decode_constr() };
                case term_tag::acase:  return { _alloc, _decode_case() };
                default: throw error(fmt::format("unexpected term: {}", static_cast<int>(typ)));
            }
        }

        plutus::version _decode_version()
        {
            const auto major = static_cast<uint64_t>(_decode_varlen_uint());
            const auto minor = static_cast<uint64_t>(_decode_varlen_uint());
            const auto patch = static_cast<uint64_t>(_decode_varlen_uint());
            return { major, minor, patch };
        }

        static void _pad(std::vector<bool> &bits)
        {
            size_t pad_bits = 0;
            switch (bits.size() % 8) {
                case 0: pad_bits = 7; break;
                case 1: pad_bits = 6; break;
                case 2: pad_bits = 5; break;
                case 3: pad_bits = 4; break;
                case 4: pad_bits = 3; break;
                case 5: pad_bits = 2; break;
                case 6: pad_bits = 1; break;
                case 7: pad_bits = 0; break;
                default: std::unreachable();
            }
            for (; pad_bits > 0; --pad_bits)
                bits.emplace_back(false);
            bits.emplace_back(true);
            if (bits.size() % 8 != 0)
                throw error(fmt::format("failed to pad the bit string to a byte boundary: {}!", bits.size()));
        }
    };

    script::script(allocator &alloc, uint8_vector &&bytes, const bool cbor):
            _impl { std::make_unique<impl>(alloc, std::move(bytes), cbor) }
    {
    }

    script::script(allocator &alloc, const buffer bytes, const bool cbor):
        script { alloc, uint8_vector { bytes }, cbor }
    {
    }

    script::~script() =default;

    version script::version() const
    {
        return _impl->version();
    }

    term script::program() const
    {
        return _impl->program();
    }
}
