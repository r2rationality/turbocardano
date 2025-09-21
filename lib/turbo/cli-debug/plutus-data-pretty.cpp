/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cli/common.hpp>
#include <turbo/cardano/common/types.hpp>
#include <turbo/plutus/types.hpp>

namespace turbo::cli::plutus_data_pretty {
    using namespace plutus;

    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "plutus-data-pretty";
            cmd.desc = "Pretty print plutus data from logs";
            cmd.args.expect({ "input-file" });
        }

        void run(const arguments &args) const override
        {
            allocator alloc {};
            const auto bytes = file::read(args.at(0));
            parser p { alloc, bytes };
            std::cout << fmt::format("{}\n", p.data().as_string(4));
        }
    private:
        struct parser {
            parser(allocator &alloc, const buffer b): _alloc { alloc }, _bytes { b }, _data { _parse() }
            {
            }

            const plutus::data &data() const
            {
                return _data;
            }
        private:
            allocator &_alloc;
            buffer _bytes;
            size_t _pos = 0;
            plutus::data _data;

            char _next() const
            {
                if (_pos < _bytes.size())
                    return _bytes[_pos];
                throw error("expecting another character after the end of data!");
            }

            char _eat_next()
            {
                if (_pos < _bytes.size())
                    return _bytes[_pos++];
                throw error("expecting another character after the end of data!");
            }

            void _eat_space()
            {
                while (std::isspace(_next())) {
                    ++_pos;
                }
            }

            void _eat(char k)
            {
                if (_next() != k) [[unlikely]]
                    throw error(fmt::format("expecting '{}' but got '{}' at pos: {}!", k, _next(), _pos));
                ++_pos;
            }

            std::string _read_tok()
            {
                _eat_space();
                std::string tok {};
                while (!std::isspace(_next())) {
                    tok += _bytes[_pos++];
                }
                if (!tok.empty()) [[likely]]
                    return tok;
                throw error("an empty token!");
            }

            data::list_type _parse_list()
            {
                data::list_type l { _alloc };
                _eat_space();
                _eat('[');
                _eat_space();
                while (_next() != ']') {
                    l.emplace_back(_parse());
                    _eat_space();
                    if (_next() == ',') {
                        _eat(',');
                        _eat_space();
                    }
                }
                _eat(']');
                return l;
            }

            plutus::data _parse_map()
            {
                data::map_type m { _alloc };
                _eat_space();
                _eat('[');
                _eat_space();
                while (_next() != ']') {
                    _eat('(');
                    auto k = _parse();
                    _eat(',');
                    auto v = _parse();
                    m.emplace_back(data_pair { _alloc, k, v });
                    _eat(')');
                    if (_next() == ',') {
                        _eat(',');
                        _eat_space();
                    }
                }
                _eat(']');
                return data::map(_alloc, std::move(m));
            }

            plutus::data _parse_constr()
            {
                auto tag = std::stoull(_read_tok());
                return data::constr(_alloc, tag, _parse_list());
            }

            plutus::data _parse_bytestring()
            {
                std::string hstr {};
                _eat_space();
                _eat('"');
                while (_next() != '"') {
                    const auto k = _bytes[_pos++];
                    hstr += k;
                    if (k == '\\')
                        hstr += _eat_next();
                }
                _eat('"');
                return data::bstr(_alloc, cardano::from_haskell(hstr));
            }

            plutus::data _parse_bigint()
            {
                std::string str {};
                _eat_space();
                bool parenthesis = _next() == '(';
                if (parenthesis)
                    _eat('(');
                if (_next() == '-')
                    str += _eat_next();
                while (std::isdigit(_next())) {
                    str += _eat_next();
                }
                if (parenthesis) {
                    _eat_space();
                    _eat(')');
                }
                return data::bint(_alloc, cpp_int { str });
            }

            plutus::data _parse()
            {
                _eat_space();
                auto tok = _read_tok();
                switch (tok[0]) {
                    case 'B':
                        if (tok == "B")
                            return _parse_bytestring();
                        throw error(fmt::format("unexpected token: {}!", tok));
                    case 'I':
                        if (tok == "I")
                            return _parse_bigint();
                        throw error(fmt::format("unexpected token: {}!", tok));
                    case 'C':
                        if (tok == "Constr")
                            return _parse_constr();
                        throw error(fmt::format("unexpected token: {}!", tok));
                    case 'L':
                        if (tok == "List")
                            return data::list(_alloc, _parse_list());
                        throw error(fmt::format("unexpected token: {}!", tok));
                    case 'M':
                        if (tok == "Map")
                            return _parse_map();
                        throw error(fmt::format("unexpected token: {}!", tok));
                    default:
                        throw error(fmt::format("unexpected token: {}!", tok));
                }
            }
        };
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}
