/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <algorithm>
#include <limits>
#include <turbo/plutus/types.hpp>

namespace turbo::plutus {
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
}
