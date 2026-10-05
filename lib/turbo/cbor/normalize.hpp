#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <string>
#include <utility>
#include <turbo/cbor/encoder.hpp>
#include <turbo/cbor/zero2.hpp>
#include <turbo/common/numeric-cast.hpp>
#include <turbo/math/big-int.hpp>

namespace turbo::cbor {
    struct normalizer_t {
        explicit normalizer_t(const size_t capacity = 0) {
            _enc.cbor().reserve(capacity);
        }

        void add(zero2::value &v) {
            switch (const auto typ = v.type(); typ) {
                case major_type::uint:
                    _enc.uint(v.uint());
                    break;
                case major_type::nint:
                    _enc.nint(v.nint_raw());
                    break;
                case major_type::bytes: {
                    if (!v.indefinite()) {
                        _enc.bytes(v.bytes());
                    } else {
                        uint8_vector b{};
                        v.to_bytes(b);
                        _enc.bytes(b);
                    }
                    break;
                }
                case major_type::text: {
                    if (!v.indefinite()) {
                        _enc.text(v.text());
                    } else {
                        std::string s{};
                        v.to_text(s);
                        _enc.text(s);
                    }
                    break;
                }
                case major_type::array:
                    _add_array(v);
                    break;
                case major_type::map:
                    _add_map(v);
                    break;
                case major_type::tag: {
                    auto &t = v.tag();
                    if (t.id() == 2 || t.id() == 3) {
                        big_int_to_cbor(_enc, big_int_from_cbor(v));
                    } else {
                        _enc.tag(t.id());
                        add(t.read());
                    }
                    break;
                }
                case major_type::simple:
                    if (v.special() == special_val::s_break) [[unlikely]]
                        throw error{"malformed CBOR: a BREAK value outside of a container!"};
                    _enc.raw_cbor(v.data_raw());
                    break;
                [[unlikely]] default: throw error{"unexpected cbor type: {}", typ};
            }
        }

        [[nodiscard]] uint8_vector finish() && {
            return std::move(_enc.cbor());
        }
    private:
        encoder _enc{};

        void _add_array(zero2::value &v) {
            auto &it = v.array();
            if (!v.indefinite()) {
                const auto sz = numeric_cast<size_t>(v.special_uint());
                _enc.array(sz);
                // Read the declared count only
                for (size_t i = 0; i < sz; ++i)
                    add(it.read());
            } else {
                normalizer_t nested{};
                size_t sz = 0;
                while (!it.done()) {
                    ++sz;
                    nested.add(it.read());
                }
                _enc.array(sz);
                _enc.raw_cbor(std::move(nested).finish());
            }
        }

        void _add_map(zero2::value &v) {
            auto &it = v.map();
            if (!v.indefinite()) {
                const auto sz = numeric_cast<size_t>(v.special_uint());
                _enc.map(sz);
                // Read the declared count only
                for (size_t i = 0; i < sz; ++i) {
                    auto &k = it.read_key();
                    add(k);
                    add(it.read_val(std::move(k)));
                }
            } else {
                normalizer_t nested{};
                size_t sz = 0;
                while (!it.done()) {
                    ++sz;
                    auto &k = it.read_key();
                    nested.add(k);
                    nested.add(it.read_val(std::move(k)));
                }
                _enc.map(sz);
                _enc.raw_cbor(std::move(nested).finish());
            }
        }
    };

    [[nodiscard]] inline uint8_vector normalize(const buffer bytes) {
        zero2::decoder dec{bytes};
        normalizer_t normalizer{bytes.size()};
        while (!dec.done()) {
            normalizer.add(dec.read());
        }
        return std::move(normalizer).finish();
    }
}
