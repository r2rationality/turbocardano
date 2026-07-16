#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <functional>
#include <turbo/cbor/types.hpp>
#include <turbo/common/bytes.hpp>

namespace turbo::cbor {
    struct encoder {
        using prepare_data_func = std::function<void()>;

        encoder &array()
        {
            _encode_item(major_type::array, static_cast<uint8_t>(special_val::s_break));
            return *this;
        }

        encoder &array(const size_t sz)
        {
            _encode_uint_item(major_type::array, sz);
            return *this;
        }

        encoder &array_compact(const size_t sz, const prepare_data_func &prepare_data)
        {
            if (sz >= 24)
                array();
            else
                array(sz);
            prepare_data();
            if (sz >= 24)
                s_break();
            return *this;
        }

        encoder &map()
        {
            _encode_item(major_type::map, static_cast<uint8_t>(special_val::s_break));
            return *this;
        }

        encoder &map(const size_t sz)
        {
            _encode_uint_item(major_type::map, sz);
            return *this;
        }

        encoder &map_compact(const size_t sz, const prepare_data_func &prepare_data)
        {
            if (sz >= 24)
                map();
            else
                map(sz);
            prepare_data();
            if (sz >= 24)
                s_break();
            return *this;
        }

        encoder &uint(const uint64_t val)
        {
            _encode_uint_item(major_type::uint, val);
            return *this;
        }

        // the negative value must be already converted to the uint64_t representation
        encoder &nint(const uint64_t val)
        {
            _encode_uint_item(major_type::nint, val);
            return *this;
        }

        encoder &float32(const float val)
        {
            _encode_item(major_type::simple, static_cast<uint8_t>(special_val::four_bytes));
            _encode_data(buffer::from(host_to_net(std::bit_cast<uint32_t>(val))));
            return *this;
        }

        encoder &bytes()
        {
            _encode_item(major_type::bytes, static_cast<uint8_t>(special_val::s_break));
            return *this;
        }

        encoder &bytes_reverse(const buffer buf)
        {
            _encode_uint_item(major_type::bytes, buf.size());
            for (size_t i = 1; i <= buf.size(); ++i)
                _buf.emplace_back(buf[buf.size() - i]);
            return *this;
        }

        encoder &bytes(const buffer buf)
        {
            _encode_uint_item(major_type::bytes, buf.size());
            _encode_data(buf);
            return *this;
        }

        encoder &text()
        {
            _encode_item(major_type::text, static_cast<uint8_t>(special_val::s_break));
            return *this;
        }

        encoder &text(const std::string_view sv)
        {
            _encode_uint_item(major_type::text, sv.size());
            _encode_data(sv);
            return *this;
        }

        encoder &raw_cbor(const buffer buf)
        {
            _encode_data(buf);
            return *this;
        }

        encoder &s_null()
        {
            _encode_item(major_type::simple, static_cast<uint8_t>(special_val::s_null));
            return *this;
        }

        encoder &s_break()
        {
            _encode_item(major_type::simple, static_cast<uint8_t>(special_val::s_break));
            return *this;
        }

        encoder &s_false()
        {
            _encode_item(major_type::simple, static_cast<uint8_t>(special_val::s_false));
            return *this;
        }

        encoder &s_true()
        {
            _encode_item(major_type::simple, static_cast<uint8_t>(special_val::s_true));
            return *this;
        }

        encoder &boolean(const bool val)
        {
            if (val)
                s_true();
            else
                s_false();
            return *this;
        }

        encoder &tag(const uint64_t id)
        {
            _encode_uint_item(major_type::tag, id);
            return *this;
        }

        encoder &custom(const std::function<void(encoder &)> &gen)
        {
            gen(*this);
            return *this;
        }

        [[nodiscard]] uint8_vector &cbor()
        {
            return _buf;
        }

        [[nodiscard]] const uint8_vector &cbor() const
        {
            return _buf;
        }
    protected:
        void _encode_data(const buffer buf)
        {
            for (const uint8_t *it = buf.data(), *end = buf.data() + buf.size(); it != end; ++it)
                _buf.emplace_back(*it);
        }
    private:
        uint8_vector _buf {};

        void _encode_uint_item(const major_type typ, const uint64_t val)
        {
            if (val < 24) {
                _encode_item(typ, val);
            } else if (val <= std::numeric_limits<uint8_t>::max()) {
                _encode_item(typ, static_cast<uint8_t>(special_val::one_byte));
                const auto h_val = host_to_net<uint8_t>(val);
                _encode_data(buffer { &h_val, sizeof(h_val) });
            } else if (val <= std::numeric_limits<uint16_t>::max()) {
                _encode_item(typ, static_cast<uint16_t>(special_val::two_bytes));
                _encode_data(buffer::from(host_to_net<uint16_t>(val)));
            } else if (val <= std::numeric_limits<uint32_t>::max()) {
                _encode_item(typ, static_cast<uint16_t>(special_val::four_bytes));
                _encode_data(buffer::from(host_to_net<uint32_t>(val)));
            } else {
                _encode_item(typ, static_cast<uint16_t>(special_val::eight_bytes));
                _encode_data(buffer::from(host_to_net<uint64_t>(val)));
            }
        }

        void _encode_item(const major_type typ, const uint8_t special)
        {
            _buf.emplace_back((static_cast<uint8_t>(typ) << 5) | (special & 0x1F));
        }
    };

    inline encoder &operator<<(encoder &dst, const encoder &src)
    {
        dst.cbor() << src.cbor();
        return dst;
    }

    inline encoder &operator<<(encoder &dst, const buffer &src)
    {
        dst.cbor() << src;
        return dst;
    }
}
