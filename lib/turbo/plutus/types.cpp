/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cbor/zero2.hpp>
#include <turbo/plutus/builtins.hpp>
#include <turbo/plutus/types.hpp>
#include <utfcpp/utf8.h>

namespace turbo::plutus {
    using builtin_name_map = std::unordered_map<std::string_view, builtin_tag>;
    static const builtin_name_map &builtin_names()
    {
        static builtin_name_map name_map {};
        if (name_map.empty()) [[unlikely]] {
            const auto &info_map = builtins::semantics_v1();
            for (const auto &[tag, info]: info_map) {
                name_map.try_emplace(info.name, tag);
            }
        }
        return name_map;
    }

    bool builtin_tag_known_name(const std::string_view name)
    {
        return builtin_names().contains(name);
    }

    builtin_tag builtin_tag_from_name(const std::string_view name)
    {
        const auto &name_map = builtin_names();
        if (const auto it = name_map.find(name); it != name_map.end()) [[likely]]
            return it->second;
        throw error(fmt::format("unknown builtin: {}", name));
    }

    static const builtin_info &_builtin_info(const builtin_tag tag)
    {
        const auto &info_map = builtins::semantics_v1();
        if (const auto it = info_map.find(tag); it != info_map.end()) [[likely]] {
            switch (it->second.num_args) {
                case 1:
                    if (!std::holds_alternative<builtin_one_arg>(it->second.func))
                        throw error("internal error: invalid plutus builtin configuration!");
                    break;
                case 2:
                    if (!std::holds_alternative<builtin_two_arg>(it->second.func))
                        throw error("internal error: invalid plutus builtin configuration!");
                    break;
                case 3:
                    if (!std::holds_alternative<builtin_three_arg>(it->second.func))
                        throw error("internal error: invalid plutus builtin configuration!");
                    break;
                case 6:
                    if (!std::holds_alternative<builtin_six_arg>(it->second.func))
                        throw error("internal error: invalid plutus builtin configuration!");
                    break;
                default:
                    throw error(fmt::format("internal error: invalid plutus builtin configuration: num_args: {}!", it->second.num_args));
            }
            return it->second;
        }
        throw error(fmt::format("not implemented: {}", tag));
    }

    size_t t_builtin::num_args() const
    {
        return _builtin_info(tag).num_args;
    }

    std::string_view t_builtin::name() const
    {
        return _builtin_info(tag).name;
    }

    size_t t_builtin::polymorphic_args() const
    {
        return _builtin_info(tag).polymorphic_args;
    }

    t_builtin t_builtin::from_name(const std::string_view name)
    {
        return { builtin_tag_from_name(name) };
    }

    bool t_delay::operator==(const t_delay &o) const
    {
        return *expr == *o.expr;
    }

    bool t_case::operator==(const t_case &o) const
    {
        return arg == o.arg && cases == o.cases;
    }

    bool t_constr::operator==(const t_constr &o) const
    {
        return tag == o.tag && args == o.args;
    }

    bool force::operator==(const force &o) const
    {
        return *expr == *o.expr;
    }

    bool t_lambda::operator==(const t_lambda &o) const
    {
        return *expr == *o.expr && var_idx == o.var_idx;
    }

    bool apply::operator==(const apply &o) const
    {
        return *func == *o.func && *arg == *o.arg;
    }

    constant_pair::constant_pair(allocator &alloc, const constant &fst, const constant &snd): constant_pair { alloc, constant { fst }, constant { snd } }
    {
    }

    bool constant_pair::operator==(const constant_pair &o) const
    {
        return _ptr->first == o._ptr->first && _ptr->second == o._ptr->second;
    }

    const constant_pair::value_type &constant_pair::operator*() const
    {
        return *_ptr;
    }

    template<typename T>
    T &require_front(list_type<T> &l)
    {
        if (l.empty()) [[unlikely]]
            throw error("the list mist have at least one element!");
        return l.front();
    }

    constant_list::constant_list(allocator &alloc, const constant_type &t):
        constant_list { alloc, value_type { t, { alloc } } }
    {
    }

    constant_list::constant_list(allocator &alloc, const constant_type &t, list_type &&l):
        constant_list { alloc, value_type { t, std::move(l) } }
    {
    }

    constant_list::constant_list(allocator &alloc, list_type &&l):
        constant_list { alloc, constant_type::from_val(alloc, require_front(l)), std::move(l) }
    {
    }

    constant_list::constant_list(allocator &alloc, const constant_type &t, std::initializer_list<constant> il):
        constant_list { alloc, value_type { t, { alloc, il } } }
    {
    }

    const constant_list::value_type *constant_list::operator->() const
    {
        return _ptr.get();
    }

    const constant_list::value_type &constant_list::operator*() const
    {
        return *_ptr;
    }

    bool constant_list::operator==(const constant_list &o) const
    {
        return _ptr->typ == o._ptr->typ && _ptr->vals == o._ptr->vals;
    }

    constant_type constant_type::from_val(allocator &alloc, const constant &c)
    {
        return std::visit([&](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return constant_type { alloc, type_tag::unit };
            } else if constexpr (std::is_same_v<T, bool>) {
                return constant_type { alloc, type_tag::boolean };
            } else if constexpr (std::is_same_v<T, bint_type>) {
                return constant_type { alloc, type_tag::integer };
            } else if constexpr (std::is_same_v<T, str_type>) {
                return constant_type { alloc, type_tag::string };
            } else if constexpr (std::is_same_v<T, bstr_type>) {
                return constant_type { alloc, type_tag::bytestring };
            } else if constexpr (std::is_same_v<T, data>) {
                return constant_type { alloc, type_tag::data };
            } else if constexpr (std::is_same_v<T, bls12_381_g1_element>) {
                return constant_type { alloc, type_tag::bls12_381_g1_element };
            } else if constexpr (std::is_same_v<T, bls12_381_g2_element>) {
                return constant_type { alloc, type_tag::bls12_381_g2_element };
            } else if constexpr (std::is_same_v<T, bls12_381_ml_result>) {
                return constant_type { alloc, type_tag::bls12_381_ml_result };
            } else if constexpr (std::is_same_v<T, constant_list>) {
                return constant_type { alloc, type_tag::list, { alloc, { v->typ } } };
            } else if constexpr (std::is_same_v<T, constant_pair>) {
                return constant_type { alloc, type_tag::pair, { alloc, { from_val(alloc, v->first), from_val(alloc, v->second) } } };
            } else {
                throw error(fmt::format("unsupported constant value: {}!", typeid(T).name()));
                // Noop to make Visual C++ happy
                return constant_type { alloc, type_tag::unit };
            }
        }, *c);
    }

    constant_list constant_list::make_one(allocator &alloc, constant &&c)
    {
        auto typ = constant_type::from_val(alloc, c);
        return { alloc, std::move(typ), { std::move(c) } };
    }

    bool data_pair::operator==(const data_pair &o) const
    {
        return *_ptr == *o._ptr;
    }

    const data_pair::value_type &data_pair::operator*() const
    {
        return *_ptr;
    }
    const data_pair::value_type *data_pair::operator->() const
    {
        return _ptr.get();
    }

    data_constr::data_constr(allocator &alloc, uint64_t t, std::initializer_list<data> il):
        _ptr { alloc.make<value_type>(bint_type { alloc, t }, list_type { alloc, std::move(il) }) }
    {
    }

    data_constr::data_constr(allocator &alloc, uint64_t t, list_type &&l):
        _ptr { alloc.make<value_type>(bint_type { alloc, t }, list_type { alloc, std::move(l) }) }
    {
    }

    data_constr::data_constr(allocator &alloc, const bint_type &t, std::initializer_list<data> il):
        _ptr { alloc.make<value_type>(t, list_type { alloc, std::move(il) }) }
    {
    }

    data_constr::data_constr(allocator &alloc, const bint_type &t, list_type &&l):
        _ptr { alloc.make<value_type>(t, list_type { alloc, std::move(l) }) }
    {
    }

    bool data_constr::operator==(const data_constr &o) const
    {
        return *_ptr == *o._ptr;
    }

    const data_constr::value_type &data_constr::operator*() const
    {
        return *_ptr;
    }

    const data_constr::value_type *data_constr::operator->() const
    {
        return _ptr.get();
    }

    data data::bstr(allocator &alloc, const bstr_type &b)
    {
        return { alloc, b };
    }

    data data::bstr(allocator &alloc, const buffer b)
    {
        return { alloc, bstr_type { alloc, b } };
    }

    data data::bint(allocator &alloc, uint64_t i)
    {
        return { alloc, bint_type { alloc, i } };
    }

    data data::bint(allocator &alloc, const bint_type &i)
    {
        return {alloc, bint_type { i } };
    }

    data data::bint(allocator &alloc, const cpp_int &i)
    {
        return bint(alloc, bint_type { alloc, i });
    }

    data data::constr(allocator &alloc, const uint64_t i, std::initializer_list<data> il)
    {
        return { alloc, data_constr { alloc, i, il } };
    }

    data data::constr(allocator &alloc, const uint64_t i, list_type &&d)
    {
        return { alloc, data_constr { alloc, i, std::move(d) } };
    }

    data data::constr(allocator &alloc, const bint_type &i, std::initializer_list<data> il)
    {
        return { alloc, data_constr { alloc, i, il } };
    }

    data data::constr(allocator &alloc, const bint_type &i, list_type &&d)
    {
        return { alloc, data_constr { alloc, i, std::move(d) } };
    }

    data data::list(allocator &alloc, list_type &&l)
    {
        return { alloc, list_type { alloc, std::move(l) } };
    }

    data data::list(allocator &alloc, std::initializer_list<data> il)
    {
        return { alloc, list_type { alloc, il } };
    }

    data data::map(allocator &alloc, std::initializer_list<data_pair> il)
    {
        return { alloc, map_type { alloc, il } };
    }

    data data::map(allocator &alloc, map_type &&m)
    {
        return { alloc, map_type { alloc, std::move(m) } };
    }

    static data _from_cbor(allocator &alloc, cbor::zero2::value &item);

    static data::list_type _from_cbor(allocator &alloc, cbor::zero2::array_reader &it)
    {
        data::list_type dl { alloc };
        while (!it.done()) {
            dl.emplace_back(_from_cbor(alloc, it.read()));
        }
        return dl;
    }

    static data _from_cbor(allocator &alloc, cbor::zero2::value &v)
    {
        switch (const auto typ = v.type(); typ) {
            case cbor::major_type::tag: {
                auto &tag = v.tag();
                switch (auto id = tag.id(); id) {
                    case 2:
                        return { alloc, bint_type { alloc, big_uint_from_cbor(tag.read()) } };
                    case 3:
                        return { alloc, bint_type { alloc, big_nint_from_cbor(tag.read()) } };
                    default: {
                        if (id >= 121 && id < 128) {
                            id -= 121;
                            return { alloc, data_constr { alloc, bint_type { alloc, id }, _from_cbor(alloc, tag.read().array()) } };
                        }
                        if (id >= 1280 && id < 1280 + 128) {
                            id -= 1280 - 7;
                            return { alloc, data_constr { alloc, bint_type { alloc, id }, _from_cbor(alloc, tag.read().array()) } };
                        }
                        if (id == 102) {
                            auto &val = tag.read();
                            auto &it = val.array();
                            id = it.read().uint();
                            auto &array_val = it.read();
                            return { alloc, data_constr { alloc, bint_type { alloc, id }, _from_cbor(alloc, array_val.array()) } };
                        }
                        throw error(fmt::format("unsupported tag id: {}", id));
                    }
                }
            }
            case cbor::major_type::array: return { alloc, _from_cbor(alloc, v.array()) };
            case cbor::major_type::map: {
                data::map_type m { alloc };
                auto &it = v.map();
                while (!it.done()) {
                    auto &mk = it.read_key();
                    auto kd = _from_cbor(alloc, mk);
                    auto &mv = it.read_val(std::move(mk));
                    auto vd = _from_cbor(alloc, mv);
                    m.emplace_back(alloc, std::move(kd), std::move(vd));
                }
                return { alloc, std::move(m) };
            }
            case cbor::major_type::bytes: {
                bstr_type::value_type buf { alloc };
                v.to_bytes(buf);
                return { alloc, bstr_type { alloc, std::move(buf) } };
            }
            case cbor::major_type::uint:
            case cbor::major_type::nint:
                return { alloc, bint_type { alloc, big_int_from_cbor(v) } };
            default: throw error(fmt::format("unsupported CBOR type {}!", typ));
        }
    }

    data data::from_cbor(allocator &alloc, const buffer bytes)
    {
        return _from_cbor(alloc, cbor::zero2::parse(bytes).get());
    }

    static void _to_cbor(cbor::encoder &enc, const data &c, size_t level=0);

    static void _bytes_to_cbor(cbor::encoder &enc, const buffer b)
    {
        if (b.size() <= 64) {
            enc.bytes(b);
        } else {
            enc.bytes();
            for (size_t i = 0; i < b.size(); i += 64)
                enc.bytes(buffer { b.data() + i, std::min(size_t { 64 }, b.size() - i) });
            enc.s_break();
        }
    }

    static void _big_uint_to_cbor(cbor::encoder &enc, const bint_type::value_type &val)
    {
        uint8_vector bytes {};
        boost::multiprecision::export_bits(val, std::back_inserter(bytes), 8, true);
        _bytes_to_cbor(enc, bytes);
    }

    static void _to_cbor(cbor::encoder &enc, const bint_type &i, const size_t)
    {
        const auto &val = *i;
        if (val >= 0) [[likely]] {
            if (val <= std::numeric_limits<uint64_t>::max()) {
                enc.uint(static_cast<uint64_t>(val));
                return;
            }
            enc.tag(2);
            _big_uint_to_cbor(enc, val);
        } else {
            bint_type::value_type val_uint { -(val + 1) };
            if (val_uint <= std::numeric_limits<uint64_t>::max()) {
                enc.nint(static_cast<uint64_t>(val_uint));
                return;
            }
            enc.tag(3);
            _big_uint_to_cbor(enc, val_uint);
        }
    }

    static void _to_cbor(cbor::encoder &enc, const bstr_type &b, const size_t)
    {
        _bytes_to_cbor(enc, *b);
    }

    static void _to_cbor(cbor::encoder &enc, const data::list_type &l, const size_t level)
    {
        if (!l.empty()) {
            enc.array();
            for (const auto &d: l)
                _to_cbor(enc, d, level + 1);
            enc.s_break();
        } else {
            enc.array(0);
        }
    }

    static void _to_cbor(cbor::encoder &enc, const data &c, const size_t level)
    {
        static constexpr size_t max_nesting_level = 1024;
        if (level >= max_nesting_level) [[unlikely]]
            throw error("only 1024 levels of CBOR nesting are supported!");
        std::visit([&](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, data::map_type>) {
                enc.map(v.size());
                for (const auto &p: v) {
                    _to_cbor(enc, p->first, level + 1);
                    _to_cbor(enc, p->second, level + 1);
                }
            } else if constexpr (std::is_same_v<T, data_constr>) {
                const auto &id = *v->first;
                if (id >= 0 && id <= 6) {
                    enc.tag(static_cast<uint64_t>(id) + 121);
                } else if (id >= 7 && id <= 127) {
                    enc.tag(static_cast<uint64_t>(id) - 7 + 1280);
                } else {
                    enc.tag(102);
                    enc.array(2);
                    _to_cbor(enc, v->first, level + 1);
                }
                _to_cbor(enc, v->second, level + 1);
            } else {
                _to_cbor(enc, v, level);
            }
        }, *c);
    }

    bstr_type data::as_cbor(allocator &alloc) const
    {
        cbor::encoder enc {};
        _to_cbor(enc, *this);
        return { alloc, std::move(enc.cbor()) };
    }

    void data::to_cbor(cbor::encoder &enc) const
    {
        _to_cbor(enc, *this);
    }

    static std::back_insert_iterator<std::string> to_string(std::back_insert_iterator<std::string> out_it, const data &v, const size_t depth, const size_t shift=4);

    static std::back_insert_iterator<std::string> to_string(std::back_insert_iterator<std::string> out_it, const data::list_type &v, const size_t depth, const size_t shift=4)
    {
        out_it = fmt::format_to(out_it, "[");
        if (!v.empty()) {
            if (shift)
                out_it = fmt::format_to(out_it, "\n");
            for (auto it = v.begin(); it != v.end(); ++it) {
                out_it = fmt::format_to(out_it, "{:{}}", "", depth * shift);
                out_it = to_string(out_it, *it, depth, shift);
                if (std::next(it) != v.end())
                    out_it = fmt::format_to(out_it, "{}", shift ? "," : ", ");
                if (shift)
                    out_it = fmt::format_to(out_it, "\n");
            }
            if (shift)
                out_it = fmt::format_to(out_it, "{:{}}", "", (depth - 1) * shift);
        }
        return fmt::format_to(out_it, "]");
    }

    static std::back_insert_iterator<std::string> to_string(std::back_insert_iterator<std::string> out_it, const data &vv, const size_t depth, const size_t shift)
    {
        return std::visit([&](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, data_constr>) {
                out_it = fmt::format_to(out_it, "Constr {} ", v->first);
                return to_string(out_it, v->second, depth + 1, shift);
            }
            if constexpr (std::is_same_v<T, data::list_type>) {
                out_it = fmt::format_to(out_it, "List ");
                return to_string(out_it, v, depth + 1, shift);
            }
            if constexpr (std::is_same_v<T, data::map_type>) {
                out_it = fmt::format_to(out_it, "Map [");
                if (!v.empty()) {
                    if (shift)
                        out_it = fmt::format_to(out_it, "\n");
                    for (auto it = v.begin(); it != v.end(); ++it) {
                        out_it = fmt::format_to(out_it, "{:{}}(", "", (depth + 1) * shift);
                        out_it = to_string(out_it, (*it)->first, depth + 1, shift);
                        out_it = fmt::format_to(out_it, ", ");
                        out_it = to_string(out_it, (*it)->second, depth + 1, shift);
                        out_it = fmt::format_to(out_it, ")");
                        if (std::next(it) != v.end())
                            out_it = fmt::format_to(out_it, "{}", shift ? "," : ", ");
                        if (shift)
                            out_it = fmt::format_to(out_it, "\n");
                    }
                    out_it = fmt::format_to(out_it, "{:{}}", "", depth * shift);
                }
                return fmt::format_to(out_it, "]");
            }
            if constexpr (std::is_same_v<T, data::int_type>)
                return fmt::format_to(out_it, "I {}", v);
            if constexpr (std::is_same_v<T, data::bstr_type>)
                return fmt::format_to(out_it, "B #{}", v);
            throw error(fmt::format("unsupported data type: {}", typeid(T).name()));
        }, *vv);
    }

    std::string data::as_string(const size_t shift) const
    {
        std::string res {};
        to_string(std::back_inserter(res), *this, 0, shift);
        return res;
    }

    bool term::operator==(const term &o) const
    {
        return *_ptr == *o._ptr;
    }

    value::value(const value &v): _ptr { v._ptr }
    {
    }

    value::value(allocator &alloc, const blst_p1 &b): value { alloc, value_type { constant { alloc, bls12_381_g1_element { b } } } }
    {
    }

    value::value(allocator &alloc, const blst_p2 &b): value { alloc, value_type { constant { alloc, bls12_381_g2_element { b } } } }
    {
    }

    value::value(allocator &alloc, const blst_fp12 &b): value { alloc, value_type { constant { alloc, bls12_381_ml_result { b } } } }
    {
    }

    value::value(allocator &alloc, data &&d): value { alloc, value_type { constant { alloc, std::move(d) } } }
    {
    }

    value::value(allocator &alloc, const bint_type &i): value { alloc, constant { alloc, i } }
    {
    }

    value::value(allocator &alloc, const cpp_int &i): value { alloc, constant { alloc, bint_type { alloc, i } } }
    {
    }

    value::value(allocator &alloc, const int64_t i): value { alloc, bint_type { alloc, i } }
    {
    }

    value::value(allocator &alloc, str_type &&s): value { alloc, constant { alloc, std::move(s) } }
    {
    }

    value::value(allocator &alloc, const std::string_view s): value { alloc, str_type { alloc, s } }
    {
    }

    value::value(allocator &alloc, const bstr_type &b): value { alloc, constant { alloc, b } }
    {
    }

    value::value(allocator &alloc, bstr_type &&b): value { alloc, constant { alloc, std::move(b) } }
    {
    }

    value::value(allocator &alloc, const buffer b): value { alloc, bstr_type { alloc, b } }
    {
    }

    value::value(allocator &alloc, value_type &&v): _ptr { alloc.make<value_type>(std::move(v)) }
    {
    }

    value::value(allocator &alloc, const constant &v): value { alloc, value_type { v } }
    {
    }

    value &value::operator=(const value &o)
    {
        _ptr = o._ptr;
        return *this;
    }

    const value::value_type &value::operator*() const
    {
        return *_ptr;
    }

    const value::value_type *value::operator->() const
    {
        return _ptr.get();
    }

    const constant &value::as_const() const
    {
        return variant::get_nice<constant>(*_ptr);
    }

    const v_constr &value::as_constr() const
    {
        return variant::get_nice<v_constr>(*_ptr);
    }

    void value::as_unit() const
    {
        const auto &c = as_const();
        if (!std::holds_alternative<std::monostate>(*c))
            throw error(fmt::format("expected a unit but got: {}", c));
    }

    bool value::as_bool() const
    {
        return as_const().as_bool();
    }

    const bint_type &value::as_int() const
    {
        return as_const().as_int();
    }

    const str_type &value::as_str() const
    {
        return as_const().as_str();
    }

    const bstr_type &value::as_bstr() const
    {
        return as_const().as_bstr();
    }

    const bls12_381_g1_element &value::as_bls_g1() const
    {
        return variant::get_nice<bls12_381_g1_element>(*as_const());
    }

    const bls12_381_g2_element &value::as_bls_g2() const
    {
        return variant::get_nice<bls12_381_g2_element>(*as_const());
    }

    const bls12_381_ml_result &value::as_bls_ml_res() const
    {
        return variant::get_nice<bls12_381_ml_result>(*as_const());
    }

    const data &value::as_data() const
    {
        return as_const().as_data();
    }

    const constant_pair::value_type &value::as_pair() const
    {
        return as_const().as_pair();
    }

    const constant_list &value::as_list() const
    {
        return as_const().as_list();
    }

    value value::boolean(allocator &alloc, const bool b)
    {
        return { alloc, value_type { constant { alloc, b } } };
    }

    value value::unit(allocator &alloc)
    {
        return { alloc, constant { alloc, std::monostate {} } };
    }

    value value::make_list(allocator &alloc, const constant_type &typ)
    {
        return { alloc, constant { alloc, constant_list { alloc, typ, {} } } };
    }

    value value::make_list(allocator &alloc, const constant_type &typ, constant_list::list_type &&l)
    {
        return { alloc, constant { alloc, constant_list { alloc, typ, std::move(l) } } };
    }

    value value::make_list(allocator &alloc, const constant_type &typ, std::initializer_list<constant> il)
    {
        return { alloc, constant { alloc, constant_list { alloc, typ, il } } };
    }

    value value::make_list(allocator &alloc, std::initializer_list<constant> il)
    {
        if (std::empty(il)) [[unlikely]]
            throw error("make_list without an explicit type requires a non-empty initializer list!");
        return { alloc, constant { alloc, constant_list { alloc, constant_type::from_val(alloc, *il.begin()), il } } };
    }

    value value::make_list(allocator &alloc, constant_list::list_type &&vals)
    {
        if (vals.empty())
            throw error("value must not be empty!");
        return { alloc, constant { alloc, constant_list { alloc, constant_type::from_val(alloc, require_front(vals)), std::move(vals) } } };
    }

    value value::make_pair(allocator &alloc, constant &&fst, constant &&snd)
    {
        return { alloc, constant { alloc, constant_pair { alloc, std::move(fst), std::move(snd) } } };
    }

    bool value::operator==(const value &o) const
    {
        return _ptr && o._ptr && *_ptr == *o._ptr;
    }

    bool v_builtin::operator==(const v_builtin &o) const
    {
        return b == o.b && args == o.args && forces == o.forces;
    }

    bool v_constr::operator==(const v_constr &o) const
    {
        return tag == o.tag && args == o.args;
    }

    bool v_delay::operator==(const v_delay &o) const
    {
        return env == o.env && *expr == *o.expr;
    }

    bool v_lambda::operator==(const v_lambda &o) const
    {
        return env == o.env && var_idx == o.var_idx && *body == *o.body;
    }

    value_list::value_list(allocator &alloc): _ptr { alloc.make<value_type>(alloc) }
    {
    }

    value_list::value_list(allocator &alloc, std::initializer_list<value> il): _ptr { alloc.make<value_type>(alloc, il) }
    {
    }

    value_list::value_list(allocator &alloc, value_type &&v): _ptr { alloc.make<value_type>(alloc, std::move(v)) }
    {
    }

    bool value_list::operator==(const value_list &o) const
    {
        return *_ptr == *o._ptr;
    }
    const value_list::value_type &value_list::operator*() const
    {
        return *_ptr;
    }

    const value_list::value_type *value_list::operator->() const
    {
        return _ptr.get();
    }

    static uint64_t uint_from_string_strict(const std::string &s)
    {
        if (s.empty() || !std::isdigit(s[0])) [[unlikely]]
            throw error(fmt::format("invalid unsigned integer: '{}'", s));
        std::size_t consumed = 0;
        const auto u = std::stoull(s, &consumed);
        if (consumed != s.size()) [[unlikely]]
            throw error(fmt::format("invalid unsigned integer: '{}'", s));
        return u;
    }

    static version from_string(const std::string &s)
    {
        try {
            const auto p1 = s.find('.');
            if (p1 == s.npos) [[unlikely]]
                throw error("must have major.minor.patch format");
            const auto major = uint_from_string_strict(s.substr(0, p1));
            const auto p2 = s.find('.', p1 + 1);
            if (p2 == s.npos) [[unlikely]]
                throw error("must have major.minor.patch format");
            const auto minor = uint_from_string_strict(s.substr(p1 + 1, p2 - p1 - 1));
            const auto patch = uint_from_string_strict(s.substr(p2 + 1));
            return { major, minor, patch };
        } catch (const std::exception &ex) {
            throw error(fmt::format("invalid version: '{}': {}", s, ex.what()));
        }
    }

    version::version(const std::string &s): version { from_string(s) }
    {
    }

    bool version::operator>=(const version &o) const
    {
        return major > o.major || (major == o.major && minor > o.minor)
            || (major == o.major && minor == o.minor && patch >= o.patch);
    }

    bool version::operator==(const version &o) const
    {
        return major == o.major && minor == o.minor && patch == o.patch;
    }

    bstr_type bls_g1_compress(allocator &alloc, const bls12_381_g1_element &v)
    {
        bstr_type::value_type res { alloc, 48 };
        blst_p1_compress(res.data(), &v.val);
        return { alloc, std::move(res) };
    }

    bstr_type bls_g2_compress(allocator &alloc, const bls12_381_g2_element &v)
    {
        bstr_type::value_type res { alloc, 96 };
        blst_p2_compress(res.data(), &v.val);
        return { alloc, std::move(res) };
    }

    bls12_381_g1_element bls_g1_decompress(const buffer bytes)
    {
        if (bytes.size() != 48) [[unlikely]]
            throw error(fmt::format("bls12_381_g1 elements must provide 48 bytes but got: {}", bytes.size()));
        blst_p1_affine out_a;
        if (const auto err = blst_p1_uncompress(&out_a, reinterpret_cast<const ::byte *>(bytes.data())); err != BLST_SUCCESS) [[unlikely]]
            throw error(fmt::format("blst12_381_g1 element decoding failed for 0x{}", bytes));
        if (!blst_p1_affine_in_g1(&out_a)) [[unlikely]]
            throw error(fmt::format("blst12_381_g1 element is invalid 0x{}", bytes));
        bls12_381_g1_element out;
        blst_p1_from_affine(&out.val, &out_a);
        return out;
    }

    bls12_381_g2_element bls_g2_decompress(const buffer bytes)
    {
        if (bytes.size() != 96) [[unlikely]]
            throw error(fmt::format("bls12_381_g2 elements must provide 96 bytes but got: {}", bytes.size()));
        blst_p2_affine out_a;
        if (const auto err = blst_p2_uncompress(&out_a, reinterpret_cast<const ::byte *>(bytes.data())); err != BLST_SUCCESS) [[unlikely]]
            throw error(fmt::format("blst12_381_g2 element decoding failed at for 0x{}", bytes));
        if (!blst_p2_affine_in_g2(&out_a)) [[unlikely]]
            throw error(fmt::format("blst12_381_g2 element is invalid 0x{}", bytes));
        bls12_381_g2_element out;
        blst_p2_from_affine(&out.val, &out_a);
        return out;
    }

    std::string escape_utf8_string(const std::string_view s)
    {
        std::string res {};
        auto res_it = std::back_inserter(res);
        for (auto it = s.begin(), end = s.end(); it != end;) {
            const auto k = utf8::next(it, end);
            if (k >= 127) {
                fmt::format_to(res_it, "\\{}", static_cast<int>(k));
            } else if (k >= 32) {
                res_it++ = k;
            } else {
                fmt::format_to(res_it, "\\x{:02X}", static_cast<int>(k));
            }
        }
        return res;
    }
}
