/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/file.hpp>
#include <dt/common/numeric-cast.hpp>
#include <dt/plutus/uplc.hpp>
#include <utfcpp/utf8.h>

namespace daedalus_turbo::plutus::uplc {
    struct script::impl {
        impl(allocator &alloc, write_vector &&bytes): _alloc { alloc }, _bytes { std::move(bytes) }
        {
            _decode_program();
            if (!_term || !_version) [[unlikely]]
                throw error("the script instance is invalid!");
        }

        plutus::version version() const
        {
            return *_version;
        }

        term program() const
        {
            return *_term;
        }
    private:
        allocator &_alloc;
        write_vector _bytes;
        size_t _pos = 0;
        std::optional<plutus::version> _version {};
        std::optional<term> _term;
        vector<str_type::value_type> _vars {};

        bool _next_is(const std::function<bool(char)> &pred, const size_t off=0)
        {
            if (_pos + off < _bytes.size()) [[likely]] {
                const char b = static_cast<char>(_bytes[_pos + off]);
                return pred(b);
            }
            return false;
        }

        bool _next_is(const char k, const size_t off=0)
        {
            return _next_is([&k](char test_k) { return test_k == k; }, off);
        }

        char _eat_next()
        {
            if (_pos< _bytes.size()) [[likely]]
                return static_cast<char>(_bytes[_pos++]);
            throw error(fmt::format("invalid script: expected more data at pos: {}", _pos));
        }

        void _eat(char k)
        {
            if (_pos < _bytes.size()) [[likely]] {
                const char b = static_cast<char>(_bytes[_pos]);
                if (k != b) [[unlikely]]
                    throw error(fmt::format("invalid script: expected {} at pos {} but got {}", k, _pos, b));
                ++_pos;
            } else {
                throw error(fmt::format("invalid script: expected {} after the end of data", k));
            }
        }

        size_t _eat_comment()
        {
            size_t start_pos = _pos;
            while (_next_is('-') && _next_is('-', 1)) {
                _eat_up_to([](char k) { return k == '\n' || k == '\r'; });
            }
            return _pos - start_pos;
        }

        size_t _eat_space()
        {
            size_t start_pos = _pos;
            _eat_comment();
            for (;;) {
                while (_pos < _bytes.size()) [[likely]] {
                    const char k = static_cast<char>(_bytes[_pos]);
                    if (!std::isspace(k)) [[unlikely]]
                        break;
                    ++_pos;
                }
                if (!_eat_comment())
                    break;
            }
            return _pos - start_pos;
        }

        void _eat_space_must()
        {
            if (_eat_space() == 0) [[unlikely]]
                throw error(fmt::format("invalid script: expected a space at pos: {}", _pos));
        }

        void _eat_lpar()
        {
            _eat_space();
            _eat('(');
            _eat_space();
        }

        void _eat_rpar()
        {
            _eat_space();
            _eat(')');
            _eat_space();
        }

        void _eat_lbr()
        {
            _eat_space();
            _eat('[');
            _eat_space();
        }

        void _eat_rbr()
        {
            _eat_space();
            _eat(']');
            _eat_space();
        }

        void _eat(const std::string_view str)
        {
            for (const auto k: str)
                _eat(k);
        }

        str_type::value_type _eat_all(const std::function<bool(char)> &pred)
        {
            str_type::value_type tok { _alloc.resource() };
            for (; _pos < _bytes.size() && pred(static_cast<char>(_bytes[_pos])); ++_pos) {
                tok += _bytes[_pos];
            }
            return tok;
        }

        str_type::value_type _eat_up_to(const std::function<bool(char)> &pred)
        {
            str_type::value_type tok { _alloc.resource() };
            for (; _pos < _bytes.size() && !pred(static_cast<char>(_bytes[_pos])); ++_pos) {
                tok += _bytes[_pos];
            }
            if (_pos >= _bytes.size() || !pred(static_cast<char>(_bytes[_pos]))) [[unlikely]]
                throw error(fmt::format("invalid script: match predicate failed at pos: {}", _pos));
            ++_pos;
            return tok;
        }

        str_type::value_type _eat_up_to(char k)
        {
            return _eat_up_to([k](char test_k) { return k == test_k; });
        }

        str_type::value_type _eat_up_to_space()
        {
            auto tok = _eat_up_to([](char test_k) { return std::isspace(test_k); });
            _eat_space();
            if (!tok.empty()) [[likely]]
                return tok;
            throw error(fmt::format("invalid script: expected a non-empty token before space at pos: {}", _pos));
        }

        str_type::value_type _eat_name()
        {
            auto name = _eat_all([](char k) { return std::isalnum(k) || k == '_' || k == '\''; });
            if (!name.empty()) [[likely]]
                return name;
            throw error(fmt::format("name cannot be empty at pos: {}", _pos));
        }

        void _decode_version()
        {
            plutus::version ver {};
            ver.major = static_cast<uint64_t>(*_decode_integer());
            _eat('.');
            ver.minor = static_cast<uint64_t>(*_decode_integer());
            _eat('.');
            ver.patch = static_cast<uint64_t>(*_decode_integer());
            _version.emplace(std::move(ver));
        }

        str_type::value_type _decode_hex()
        {
            return _eat_all([](char k) { return std::isxdigit(k); });
        }

        bstr_type _decode_bytestring()
        {
            _eat('#');
            return bstr_type::from_hex(_alloc, _decode_hex());
        }

        char _decode_hex_char()
        {
            const auto a = _eat_next();
            const auto b = _eat_next();
            return static_cast<char>(uint_from_hex(a) << 4 | uint_from_hex(b));
        }

        char _decode_oct_char()
        {
            const auto a = _eat_next();
            const auto b = _eat_next();
            const auto c = _eat_next();
            return static_cast<char>(uint_from_oct(a) * 8 * 8 | uint_from_oct(b) * 8 | uint_from_oct(c));
        }

        void _decode_dec_char(str_type::value_type &s, utf8::utfchar32_t k)
        {
            while (_next_is([](const char k) { return std::isdigit(k); })) {
                k *= 10;
                k += static_cast<utf8::utfchar32_t>(_eat_next() - '0');
            }
            utf8::append(k, std::back_inserter(s));
        }

        void _decode_escaped_char(str_type::value_type &s)
        {
            switch (const char k = _eat_next(); k) {
                case 'a': s += static_cast<char>(0x07); break;
                case 'b': s += static_cast<char>(0x08); break;
                case 'e': s += static_cast<char>(0x1B); break;
                case 'f': s += static_cast<char>(0x0C); break;
                case 'n': s += static_cast<char>(0x0A); break;
                case 'r': s += static_cast<char>(0x0D); break;
                case 't': s += static_cast<char>(0x09); break;
                case 'v': s += static_cast<char>(0x0B); break;
                case '\\': s += '\\'; break;
                case '\'': s += '\''; break;
                case '"': s += '\"'; break;
                case '?': s += '?'; break;
                case 'x': s += _decode_hex_char(); break;
                case 'o': s += _decode_oct_char(); break;
                case '0':
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7':
                case '8':
                case '9':
                    _decode_dec_char(s, k - '0'); break;
                default: s += k; break;
            }
        }

        str_type _decode_string()
        {
            _eat('"');
            str_type::value_type s { _alloc.resource() };
            while (!_next_is('"')) {
                switch (const auto k = _eat_next(); k) {
                    case '\\': _decode_escaped_char(s); break;
                    default: s += k;
                }
            }
            _eat('"');
            return { _alloc, std::move(s) };
        }

        bint_type _decode_integer()
        {
            std::string str {};
            if (_next_is('+'))
                ++_pos;
            else if (_next_is('-'))
                str += _bytes[_pos++];
            // skip forward zeros since otherwise boost::multiprecision will interpret such string as of radix 8
            while (_next_is('0') && _next_is([](char k) { return std::isdigit(k); }, 1))
                ++_pos;
            str += _eat_all([](char k) { return std::isdigit(k); });
            if (str.empty()) [[unlikely]]
                throw error(fmt::format("expected an integer at pos: {}", _pos));
            return bint_type { _alloc, str };
        }

        bool _decode_boolean()
        {
            if (_next_is('F')) {
                _eat("False");
                return false;
            }
            if (_next_is('T')) {
                _eat("True");
                return true;
            }
            throw error(fmt::format("unexpected boolean value at pos: {}", _pos));
        }

        bls12_381_g1_element _decode_bls12_381_g1()
        {
            _eat("0x");
            return bls_g1_decompress(uint8_vector::from_hex(_decode_hex()));
        }

        bls12_381_g2_element _decode_bls12_381_g2()
        {
            _eat("0x");
            return bls_g2_decompress(uint8_vector::from_hex(_decode_hex()));
        }

        data::list_type _decode_data_list()
        {
            _eat_lbr();
            data::list_type l { _alloc };
            while (!_next_is(']')) {
                l.emplace_back(_decode_data_item());
                _eat_space();
                if (_next_is(',')) {
                    _eat(',');
                    _eat_space();
                }
            }
            _eat_rbr();
            return l;
        }

        data::map_type _decode_data_map()
        {
            _eat_lbr();
            data::map_type m { _alloc };
            while (!_next_is(']')) {
                _eat_lpar();
                auto k = _decode_data_item();
                _eat_space();
                _eat(',');
                _eat_space();
                auto v = _decode_data_item();
                m.emplace_back(_alloc, std::move(k), std::move(v));
                _eat_rpar();
                if (_next_is(',')) {
                    _eat(',');
                    _eat_space();
                }
            }
            _eat_rbr();
            return m;
        }

        data _decode_data_item()
        {
            const auto tok = _eat_name();
            switch ((tok)[0]) {
                case 'B': {
                    if (tok != "B") [[unlikely]]
                        throw error(fmt::format("unsupported token in data definition: {} at pos: {}", tok, _pos));
                    _eat_space();
                    return { _alloc, _decode_bytestring() };
                }
                case 'C': {
                    if (tok != "Constr") [[unlikely]]
                        throw error(fmt::format("unsupported token in data definition: {} at pos: {}", tok, _pos));
                    _eat_space();
                    auto id = _decode_integer();
                    _eat_space();
                    auto l = _decode_data_list();
                    return { _alloc, data_constr { _alloc, std::move(id), std::move(l) } };
                }
                case 'I': {
                    if (tok != "I") [[unlikely]]
                        throw error(fmt::format("unsupported token in data definition: {} at pos: {}", tok, _pos));
                    _eat_space();
                    return { _alloc, _decode_integer() };
                }
                case 'L': {
                    if (tok != "List") [[unlikely]]
                        throw error(fmt::format("unsupported token in data definition: {} at pos: {}", tok, _pos));
                    _eat_space();
                    return { _alloc, _decode_data_list() };
                }
                case 'M': {
                    if (tok != "Map") [[unlikely]]
                        throw error(fmt::format("unsupported token in data definition: {} at pos: {}", tok, _pos));
                    _eat_space();
                    return { _alloc, _decode_data_map() };
                }
                default: throw error(fmt::format("unsupported token '{}' at pos: {}", tok, _pos));
            }
        }

        data _decode_data()
        {
            cbor::encoder enc {};
            _eat_lpar();
            auto d = _decode_data_item();
            _eat_rpar();
            return d;
        }

        constant_type _decode_list_type()
        {
            _eat_space();
            constant_type::list_type n { _alloc, { _decode_constant_type() } };
            return { _alloc, type_tag::list, std::move(n) };
        }

        constant_type _decode_pair_type()
        {
            _eat_space();
            constant_type::list_type n { _alloc };
            n.emplace_back(_decode_constant_type());
            _eat_space();
            n.emplace_back(_decode_constant_type());
            return { _alloc, type_tag::pair, std::move(n) };
        }

        constant_type _decode_constant_type_inner()
        {
            const auto typ = _eat_name();
            if (typ == "list")
                return _decode_list_type();
            if (typ == "pair")
                return _decode_pair_type();
            throw error(fmt::format("unexpected token '{}' at pos: {}", typ, _pos));
        }

        constant_type _decode_constant_type()
        {
            if (_next_is('(')) {
                _eat_lpar();
                auto t = _decode_constant_type_inner();
                _eat_rpar();
                return t;
            }
            const auto typ = _eat_name();
            switch ((typ)[0]) {
                case 'b':
                    if (typ == "bytestring") [[likely]]
                        return { _alloc, type_tag::bytestring };
                    if (typ == "bool") [[likely]]
                        return { _alloc, type_tag::boolean };
                    if (typ == "bls12_381_G1_element")
                        return { _alloc, type_tag::bls12_381_g1_element };
                    if (typ == "bls12_381_G2_element")
                        return { _alloc, type_tag::bls12_381_g2_element };
                    throw error(fmt::format("unexpected token '{}' at pos: {}", typ, _pos));
                case 'd':
                    if (typ != "data") [[unlikely]]
                        throw error(fmt::format("unexpected token '{}' at pos: {}", typ, _pos));
                    return { _alloc, type_tag::data };
                case 'i':
                    if (typ != "integer") [[unlikely]]
                        throw error(fmt::format("unexpected token '{}' at pos: {}", typ, _pos));
                    return { _alloc, type_tag::integer };
                case 's':
                    if (typ != "string") [[unlikely]]
                        throw error(fmt::format("unexpected token '{}' at pos: {}", typ, _pos));
                    return { _alloc, type_tag::string };
                case 'u':
                    if (typ != "unit") [[unlikely]]
                        throw error(fmt::format("unexpected token '{}' at pos: {}", typ, _pos));
                    return { _alloc, type_tag::unit };
                default: throw error(fmt::format("unexpected token '{}' at pos: {}", typ, _pos));
            }
        }

        constant_pair _decode_pair_value(constant_type &&typ)
        {
            if (typ->nested.size() != 2) [[unlikely]]
                    throw error(fmt::format("the nested type list for a pair must have two elements but has {}", typ->nested.size()));
            _eat_lpar();
            auto fst = _decode_constant_value(constant_type { typ->nested.front() });
            _eat_space();
            _eat(',');
            _eat_space();
            auto snd = _decode_constant_value(constant_type { typ->nested.back() });
            _eat_rpar();
            return { _alloc, std::move(fst), std::move(snd) };
        }

        constant_list _decode_list_value(constant_type &&list_typ)
        {
            if (list_typ->nested.size() != 1) [[unlikely]]
                    throw error(fmt::format("the nested type list for a list must have just one element but has {}", list_typ->nested.size()));
            _eat_lbr();
            auto typ = list_typ->nested.front();
            constant_list::list_type vals { _alloc };
            while (!_next_is(']')) {
                vals.emplace_back(_decode_constant_value(constant_type { typ }));
                _eat_space();
                if (_next_is(',')) {
                    _eat(',');
                    _eat_space();
                }
            }
            _eat_rbr();
            return { _alloc, { std::move(typ), std::move(vals) } };
        }

        std::monostate _decode_unit()
        {
            _eat_lpar();
            _eat_rpar();
            return {};
        }

        constant _decode_constant_value(constant_type &&typ)
        {
            switch (const auto tag = typ->typ; tag) {
                case type_tag::bls12_381_g1_element: return { _alloc, _decode_bls12_381_g1() };
                case type_tag::bls12_381_g2_element: return { _alloc, _decode_bls12_381_g2() };
                case type_tag::bytestring: return { _alloc, _decode_bytestring() };
                case type_tag::boolean: return { _alloc, _decode_boolean() };
                case type_tag::data: return { _alloc, _decode_data() };
                case type_tag::integer: return { _alloc, _decode_integer() };
                case type_tag::string: return { _alloc, _decode_string() };
                case type_tag::unit: return { _alloc, _decode_unit() };
                case type_tag::list: return { _alloc, _decode_list_value(std::move(typ)) };
                case type_tag::pair: return { _alloc, _decode_pair_value(std::move(typ)) };
                default: throw error(fmt::format("unexpected type: {}", tag));
            }
        }

        term _decode_constant()
        {
            _eat_space();
            auto typ = _decode_constant_type();
            _eat_space();
            return { _alloc, _decode_constant_value(std::move(typ)) };
        }

        term _decode_constr()
        {
            if (_version && (_version->empty() || (_version->major > 1 || (_version->major == 1 && _version->minor >= 1)))) {
                _eat_space();
                const auto tag = static_cast<uint64_t>(*_decode_integer());
                _eat_space();
                term_list::value_type args { _alloc };
                while (!_next_is(')')) {
                    args.emplace_back(_decode_term());
                }
                return { _alloc, t_constr { tag, term_list { _alloc, std::move(args) } } };
            }
            throw error(fmt::format("constr term is allowed only for programs of versions 1.1.0 and higher but have: {}", _version));
        }

        term _decode_case()
        {
            if (_version && (_version->empty() || (_version->major > 1 || (_version->major == 1 && _version->minor >= 1)))) {
                _eat_space();
                auto arg = _decode_term();
                _eat_space();
                term_list::value_type cases { _alloc };
                while (!_next_is(')')) {
                    cases.emplace_back(_decode_term());
                }
                return { _alloc, t_case { std::move(arg), { _alloc, std::move(cases) } } };
            }
            throw error(fmt::format("case term is allowed only for programs of versions 1.1.0 and higher but have: {}", _version));
        }

        term _decode_builtin()
        {
            const auto name = _eat_name();
            return { _alloc, t_builtin::from_name(name) };
        }

        term _decode_lambda() {
            auto name = _eat_name();
            _eat_space_must();
            const auto var_idx = _vars.size();
            _vars.emplace_back(name);
            const auto body = _decode_term();
            if (_vars.empty() || _vars.back() != name) [[unlikely]]
                throw error(fmt::format("internal error: expected variable {} is missing!", name));
            _vars.pop_back();
            return { _alloc, t_lambda { var_idx, body } };
        }

        term _decode_force()
        {
            return { _alloc, force { _decode_term() } };
        }

        term _decode_delay()
        {
            return { _alloc, t_delay { _decode_term() } };
        }

        term _decode_error()
        {
            return { _alloc, failure {} };
        }

        term _decode_term_tag(const str_type::value_type &tag)
        {
            switch (tag[0]) {
                case 'b':
                    if (tag != "builtin") [[unlikely]]
                        throw error(fmt::format("unexpected token '{}' at pos: {}", tag, _pos));
                    return _decode_builtin();
                case 'c':
                    if (tag == "con") [[likely]]
                        return _decode_constant();
                    if (tag == "constr")
                        return _decode_constr();
                    if (tag == "case")
                        return _decode_case();
                    throw error(fmt::format("unexpected token '{}' at pos: {}", tag, _pos));
                case 'd':
                    if (tag != "delay") [[unlikely]]
                        throw error(fmt::format("unexpected token '{}' at pos: {}", tag, _pos));
                    return _decode_delay();
                case 'e':
                    if (tag != "error") [[unlikely]]
                        throw error(fmt::format("unexpected token '{}' at pos: {}", tag, _pos));
                    return _decode_error();
                case 'f':
                    if (tag != "force") [[unlikely]]
                        throw error(fmt::format("unexpected token '{}' at pos: {}", tag, _pos));
                    return _decode_force();
                case 'l':
                    if (tag != "lam") [[unlikely]]
                        throw error(fmt::format("unexpected token '{}' at pos: {}", tag, _pos));
                    return _decode_lambda();
                default:
                    throw error(fmt::format("unexpected token '{}' at pos: {}", tag, _pos));
            }
        }

        term _decode_term_par()
        {
            _eat_lpar();
            const auto tag = _eat_all([](char k) { return std::isalnum(k); });
            _eat_space();
            term t = _decode_term_tag(tag);
            _eat_rpar();
            return t;
        }

        term _decode_term_apply()
        {
            _eat_lbr();
            const auto func = _decode_term();
            auto arg = _decode_term();
            term appl { _alloc, apply { func, arg } };
            _eat_space();
            while (!_next_is(']')) {
                arg = _decode_term();
                appl = { _alloc, apply { appl, arg } };
            }
            _eat_rbr();
            return appl;
        }

        term _decode_term_inner()
        {
            if (_next_is('('))
                return _decode_term_par();
            if (_next_is('['))
                return _decode_term_apply();
            const auto name = _eat_name();
            const auto it = std::find(_vars.rbegin(), _vars.rend(), name);
            if (it == _vars.rend()) [[unlikely]]
                throw error(fmt::format("unknown variable '{}' at pos: {}", name, _pos));
            return { _alloc, variable { numeric_cast<size_t>(it.base() - 1 - _vars.begin()) } };
        }

        term _decode_term()
        {
            _eat_space();
            term t = _decode_term_inner();
            _eat_space();
            return t;
        }

        void _decode_program()
        {
            _eat_lpar();
            _eat("program");
            _eat_space();
            _decode_version();
            _term = _decode_term();
            _eat_rpar();
        }
    };

    script::script(allocator &alloc, write_vector &&bytes): _impl { std::make_unique<impl>(alloc, std::move(bytes)) }
    {
    }

    script::script(script &&s): _impl { std::move(s._impl) }
    {
    }

    script::~script() = default;

    version script::version() const
    {
        return _impl->version();
    }

    term script::program() const
    {
        return _impl->program();
    }
}
