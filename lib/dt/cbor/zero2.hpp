#pragma once
#ifndef DAEDALUS_TURBO_CBOR_ZERO2_HPP
#define DAEDALUS_TURBO_CBOR_ZERO2_HPP
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

/*
 * This is a zero-copy one-pass CBOR parser. It has a very small memory overhead and
 * ensures that nested data structures are accessed only once by default.
 * It was primarily developed for comparison of multi-gigabyte CBOR files in tight-on-RAM environments.
 * Furthermore, the low memory overhead, makes it well suited for parallel processing of CBOR data.
 *
 * The code is defined in a single header to benefit from maximum inlining for performance.
 */

#include <array>
#include <optional>
#include <variant>
#include <dt/common/bytes.hpp>
#include <dt/common/error.hpp>
#include <dt/common/format.hpp>
#include <dt/common/variant.hpp>
#include "dt/container.hpp"
#include "types.hpp"

namespace daedalus_turbo::cbor::zero2 {
    typedef daedalus_turbo::error error;

    struct incomplete_error: error {
        incomplete_error():
            error("cbor value stream has terminated prematurely!")
        {
        }
    };

    using major_type = cbor::major_type;
    using special_val = cbor::special_val;

    struct decoder;
    struct value;

    struct reader {
    protected:
        friend value;
        // take self as a parameter to ensure the right (the subclass) "this" pointer is used
        // as a consequence, all methods calling _parent must be overridden virtual methods
        static const value &_parent(const void *self);

        static value &_parent(void *self)
        {
            return const_cast<value &>(_parent(static_cast<const void *>(self)));
        }

        // no need for a virtual destructor since no child class needs it

        virtual void consume() =0;

        value *next_chunk(value &);
    };

    struct simple_reader: reader {
        void consume() override;
    };

    struct special_reader: simple_reader {
        special_val read();
    };

    struct uint_reader: simple_reader {
        uint64_t read();
    };

    struct nint_reader: simple_reader {
        uint64_t read();
        uint64_t read_raw();
    };

    struct float32_reader: special_reader {
        float read();
    };

    struct text_reader: simple_reader {
        virtual void read(std::pmr::string &s);
        virtual void read(std::string &s);
        virtual std::string_view read();
    };

    struct chunked_text_reader: text_reader {
        chunked_text_reader(const chunked_text_reader &) =delete;

        chunked_text_reader(value &dec_level): _dec_level { dec_level }
        {
        }

        void read(std::pmr::string &s) override;
        void read(std::string &s) override;
        std::string_view read() override;
        void consume() override;
    private:
        value &_dec_level;
    };

    struct bytes_reader: simple_reader {
        virtual void read(std::pmr::vector<uint8_t> &b);
        virtual void read(write_vector &b);
        virtual buffer read();
    };

    struct chunked_bytes_reader: bytes_reader {
        chunked_bytes_reader(const chunked_text_reader &) =delete;

        chunked_bytes_reader(value &dec_level): _dec_level { dec_level }
        {
        }

        void read(std::pmr::vector<uint8_t> &b) override;
        void read(write_vector &b) override;
        buffer read() override;
        void consume() override;
    private:
        value &_dec_level;
    };

    // this reader is placed once the value has been consumed
    struct consumed_reader: reader {
        void consume() override
        {
        }
    };

    struct tag_reader: reader {
        tag_reader(const tag_reader &) =delete;

        tag_reader(value &dec_level);

        void consume() override;
        uint64_t id() const noexcept;
        value &read();
    private:
        const uint8_t *_begin_ptr;
        value &_val;
    };
    static_assert(sizeof(tag_reader) == 24);

    struct array_reader: reader {
        array_reader(const array_reader &) =delete;

        array_reader(value &dec_level) noexcept:
            _dec_level { dec_level }
        {
        }

        // a virtual destructor is not needed since values are stores as direct class instances
        virtual bool done();
        virtual value &read();
        array_reader &skip(const size_t num_items);
        void consume() override;
    protected:
        value &_dec_level;
    };
    static_assert(sizeof(array_reader) == 16);

    struct array_reader_sized: array_reader {
        using array_reader::array_reader;

        bool done() override;
        value &read() override;
        void consume() override;
    private:
        size_t _pos = 0;
    };
    static_assert(sizeof(array_reader_sized) == 24);

    struct map_reader: reader {
        map_reader(const map_reader &) =delete;

        map_reader(value &dec_level) noexcept:
            _dec_level { dec_level }
        {
        }

        // a virtual destructor is not needed since values are stored as direct class instances
        virtual bool done();
        virtual value &read_key();
        // the move argument is just a reminder that the previously returned key argument will be overwritten
        virtual value &read_val(value &&);
        void consume() override;
        void skip(const size_t num_items);
    protected:
        value &_dec_level;
    };
    static_assert(sizeof(map_reader) == 16);

    struct map_reader_sized: map_reader {
        using map_reader::map_reader;

        bool done() override;
        // must be overridden to ensure _parent gets the right version of "this" pointer
        value &read_key() override;
        value &read_val(value &&) override;
        void consume() override;
    private:
        size_t _pos = 0;
    };
    static_assert(sizeof(map_reader_sized) == 24);

    struct value {
        value(const value &o) =delete;

        value(const uint8_t *data_begin, decoder &parent);

        value &at(size_t pos);

        bool boolean()
        {
            switch (const auto spec = _reader_cast<special_reader>().read(); spec) {
                case special_val::s_true: return true;
                case special_val::s_false: return false;
                [[unlikely]] default: throw error(fmt::format("expected for a boolean but got: {}!", spec));
            }
        }

        uint64_t uint()
        {
            return _reader_cast<uint_reader>().read();
        }

        uint64_t nint()
        {
            return _reader_cast<nint_reader>().read();
        }

        uint64_t nint_raw()
        {
            return _reader_cast<nint_reader>().read_raw();
        }

        buffer bytes()
        {
            return _reader_cast<bytes_reader>().read();
        }

        void to_bytes(std::pmr::vector<uint8_t> &res)
        {
            return _reader_cast<bytes_reader>().read(res);
        }

        void to_bytes(write_vector &res)
        {
            return _reader_cast<bytes_reader>().read(res);
        }

        std::string_view text()
        {
            return _reader_cast<text_reader>().read();
        }

        void to_text(std::pmr::string &res)
        {
            return _reader_cast<text_reader>().read(res);
        }

        void to_text(std::string &res)
        {
            return _reader_cast<text_reader>().read(res);
        }

        array_reader &array()
        {
            return _reader_cast<array_reader>();
        }

        array_reader &array_sized()
        {
            return _reader_cast<array_reader_sized>();
        }

        map_reader &map()
        {
            return _reader_cast<map_reader>();
        }

        float float32()
        {
            return _reader_cast<float32_reader>().read();
        }

        tag_reader &tag()
        {
            return _reader_cast<tag_reader>();
        }

        special_val special()
        {
            return _reader_cast<special_reader>().read();
        }

        bool is_null() const
        {
            return type_byte() == 0xF6;
        }

        uint8_t type_byte() const
        {
            return *_data_begin;
        }

        major_type type() const noexcept
        {
            return static_cast<major_type>((_data_begin[0] >> 5) & 0x7);
        }

        bool indefinite() const
        {
            switch (type()) {
                case major_type::bytes:
                case major_type::text:
                case major_type::array:
                case major_type::map:
                    return special_tag() == special_val::s_break;
                [[unlikely]] default:
                    throw error(fmt::format("indefinite is supported only for bytes, text, array, and map but got {}", type()));
            }
        }

        buffer data_raw()
        {
            return { _data_begin, static_cast<size_t>(end() - _data_begin) };
        }

        const uint8_t *data_begin() const noexcept
        {
            return _data_begin;
        }

        uint64_t special_uint() const noexcept
        {
            return _spec_uint;
        }

        special_val special_tag() const noexcept
        {
            return static_cast<special_val>(_data_begin[0] & 0x1F);
        }

        buffer data_special() const;
        std::string to_string(size_t max_seq_to_expand=std::numeric_limits<size_t>::max());
        void to_stream(std::ostream &os, size_t max_seq_to_expand=std::numeric_limits<size_t>::max());
    protected:
        friend decoder;
        friend reader;
        friend simple_reader;
        friend special_reader;
        friend array_reader;
        friend array_reader_sized;
        friend map_reader;
        friend map_reader_sized;
        friend tag_reader;
        friend chunked_bytes_reader;
        friend chunked_text_reader;
        friend float32_reader;

        static const uint8_t *null_val_ptr();
        inline value(decoder &);
        inline void finalize();
        inline const uint8_t *end();
        inline void _mark_end();
        inline buffer data_raw_ext() const;
    private:
        using reader_storage = byte_array<24>;
        static_assert(sizeof(reader_storage) >= sizeof(simple_reader));
        static_assert(sizeof(reader_storage) >= sizeof(tag_reader));
        static_assert(sizeof(reader_storage) >= sizeof(chunked_text_reader));
        static_assert(sizeof(reader_storage) >= sizeof(chunked_bytes_reader));
        static_assert(sizeof(reader_storage) >= sizeof(array_reader));
        static_assert(sizeof(reader_storage) >= sizeof(array_reader_sized));
        static_assert(sizeof(reader_storage) >= sizeof(map_reader));
        static_assert(sizeof(reader_storage) >= sizeof(map_reader_sized));

        const uint8_t *_data_begin;
        uint64_t _spec_uint;
        reader_storage _reader;

        inline reader &_reader_base()
        {
            return *reinterpret_cast<reader *>(&_reader);
        }

        template<typename T>
        inline T &_reader_cast()
        {
            if (auto *ptr = dynamic_cast<T *>(&_reader_base()); ptr) [[likely]]
                return *ptr;
            throw error(fmt::format("expected {} but got {}", typeid(T).name(), typeid(_reader_base()).name()));
        }

        static size_t special_bytes(const special_val sv)
        {
#if defined(__clang__) || defined(__GNUC__)
#       pragma GCC diagnostic push
#       pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
            static_assert(offsetof(value, _reader) == reader_offset);
#if defined(__clang__) || defined(__GNUC__)
#       pragma GCC diagnostic pop
#endif
            switch (sv) {
                case special_val::one_byte: return 2;
                case special_val::two_bytes: return 3;
                case special_val::four_bytes: return 5;
                case special_val::eight_bytes: return 9;
                default: return 1;
            }
        }
    protected:
        static constexpr size_t reader_offset = 16;
        decoder & _dec; // keep it the last since it is referenced in the last
    };
    static_assert(sizeof(value) <= 64); // Ensure each value fits into a single cache line

    struct decoder {
        static constexpr size_t max_depth = 64;

        explicit decoder(const buffer data):
            _ptr { data.data() },
            _end { data.data() + data.size() },
            _current { &_val_at(0) }
        {
            for (size_t i = 0; i < max_depth; ++i) {
                new (&_val_at(i)) value { *this };
            }
        }

        bool done() noexcept
        {
            return done(_val_at(0));
        }

        value &read()
        {
            return read(_val_at(0));
        }
    protected:
        friend value;
        friend reader;
        friend chunked_text_reader;
        friend chunked_bytes_reader;
        friend array_reader;
        friend array_reader_sized;
        friend map_reader;
        friend map_reader_sized;
        friend tag_reader;

        const uint8_t *_ptr;
        const uint8_t *_end;
        std::array<uint8_t, sizeof(value) * max_depth> _val_storage {};
        value *_current;

        bool done(value &v)
        {
            v.finalize();
            return empty();
        }

        value &read(value &v)
        {
            v.finalize();
            if (empty()) [[unlikely]]
                throw incomplete_error();
            new (&v) value { _ptr, *this };
            return v;
        }

        value &push()
        {
            ++_current;
            // ensures that the values are never reallocated since value::readers keep references to them!
            if (_current >= &_val_at(max_depth)) [[unlikely]]
                throw error(fmt::format("the cbor structure has more than {} levels!", max_depth));
            return *_current;
        }

        void pop()
        {
            if (_current == &_val_at(0)) [[unlikely]]
                throw error("cbor::decoder: invalid nesting of cbor structures detected!");
            new (_current) value { *this };
            --_current;
        }

        bool empty() const noexcept
        {
            return _ptr >= _end;
        }

        // a known fixed subset of callers has checks to dereference the returned pointer only if it points to valid data
        void step1()
        {
            ++_ptr;
        }

        void step(const ptrdiff_t num_bytes)
        {
            _ptr += num_bytes;
            if (_ptr > _end) [[unlikely]]
                throw incomplete_error();
        }

        // a known fixed subset of callers has checks to dereference the returned pointer only if it points to valid data
        const uint8_t *next() const
        {
            return _ptr;
        }

        const uint8_t *end() const
        {
            return _end;
        }
    private:
        // this is a private method. all callers ensure i is alway < max_depth
        value &_val_at(size_t i)
        {
            return *(reinterpret_cast<value *>(&_val_storage) + i);
        }
    };

    inline const uint8_t *value::null_val_ptr()
    {
        static uint8_t null_val = 0xF6;
        return &null_val;
    }

    inline value::value(decoder &parent):
        _data_begin { null_val_ptr() },
        _dec { parent }
    {
        new (&_reader_base()) special_reader {};
    }

    // data_begin must point to at a non-empty byte buffer
    inline value::value(const uint8_t *data_begin, decoder &parent):
        _data_begin { data_begin },
        _dec { parent }
    {
#           if defined(__clang__) || defined(__GNUC__)
#               pragma GCC diagnostic push
#               pragma GCC diagnostic ignored "-Winvalid-offsetof"
#           endif
#           if defined(__clang__) || defined(__GNUC__)
#               pragma GCC diagnostic pop
#           endif
        switch (_data_begin[0]) {
            case 0x00:
            case 0x01:
            case 0x02:
            case 0x03:
            case 0x04:
            case 0x05:
            case 0x06:
            case 0x07:
            case 0x08:
            case 0x09:
            case 0x0A:
            case 0x0B:
            case 0x0C:
            case 0x0D:
            case 0x0E:
            case 0x0F:
            case 0x10:
            case 0x11:
            case 0x12:
            case 0x13:
            case 0x14:
            case 0x15:
            case 0x16:
            case 0x17:
                _dec.step1();
                _spec_uint = _data_begin[0];
                new (&_reader_base()) uint_reader {};
                break;
            case 0x18:
                _dec.step(2);
                _spec_uint = _data_begin[1];
                new (&_reader_base()) uint_reader {};
                break;
            case 0x19:
                _dec.step(3);
                _spec_uint = net_to_host<uint16_t>(*reinterpret_cast<const uint16_t *>(&_data_begin[1]));
                new (&_reader_base()) uint_reader {};
                break;
            case 0x1A:
                _dec.step(5);
                _spec_uint = net_to_host<uint32_t>(*reinterpret_cast<const uint32_t *>(&_data_begin[1]));
                new (&_reader_base()) uint_reader {};
                break;
            case 0x1B:
                _dec.step(9);
                _spec_uint = net_to_host<uint64_t>(*reinterpret_cast<const uint64_t *>(&_data_begin[1]));
                new (&_reader_base()) uint_reader {};
                break;
            case 0x20:
            case 0x21:
            case 0x22:
            case 0x23:
            case 0x24:
            case 0x25:
            case 0x26:
            case 0x27:
            case 0x28:
            case 0x29:
            case 0x2A:
            case 0x2B:
            case 0x2C:
            case 0x2D:
            case 0x2E:
            case 0x2F:
            case 0x30:
            case 0x31:
            case 0x32:
            case 0x33:
            case 0x34:
            case 0x35:
            case 0x36:
            case 0x37:
                _spec_uint = _data_begin[0] & 0x1F;
                _dec.step1();
                new (&_reader_base()) nint_reader {};
                break;
            case 0x38:
                _dec.step(2);
                _spec_uint = _data_begin[1];
                new (&_reader_base()) nint_reader {};
                break;
            case 0x39:
                _dec.step(3);
                _spec_uint = net_to_host<uint16_t>(*reinterpret_cast<const uint16_t *>(&_data_begin[1]));
                new (&_reader_base()) nint_reader {};
                break;
            case 0x3A:
                _dec.step(5);
                _spec_uint = net_to_host<uint32_t>(*reinterpret_cast<const uint32_t *>(&_data_begin[1]));
                new (&_reader_base()) nint_reader {};
                break;
            case 0x3B:
                _dec.step(9);
                _spec_uint = net_to_host<uint64_t>(*reinterpret_cast<const uint64_t *>(&_data_begin[1]));
                new (&_reader_base()) nint_reader {};
                break;
            case 0x40:
            case 0x41:
            case 0x42:
            case 0x43:
            case 0x44:
            case 0x45:
            case 0x46:
            case 0x47:
            case 0x48:
            case 0x49:
            case 0x4A:
            case 0x4B:
            case 0x4C:
            case 0x4D:
            case 0x4E:
            case 0x4F:
            case 0x50:
            case 0x51:
            case 0x52:
            case 0x53:
            case 0x54:
            case 0x55:
            case 0x56:
            case 0x57:
                _spec_uint = _data_begin[0] & 0x1F;
                _dec.step(1 + _spec_uint);
                new (&_reader_base()) bytes_reader {};
                break;
            case 0x58:
                _dec.step(2);
                _spec_uint = _data_begin[1];
                _dec.step(_spec_uint);
                new (&_reader_base()) bytes_reader {};
                break;
            case 0x59:
                _dec.step(3);
                _spec_uint = net_to_host<uint16_t>(*reinterpret_cast<const uint16_t *>(&_data_begin[1]));
                _dec.step(_spec_uint);
                new (&_reader_base()) bytes_reader {};
                break;
            case 0x5A:
                _dec.step(5);
                _spec_uint = net_to_host<uint32_t>(*reinterpret_cast<const uint32_t *>(&_data_begin[1]));
                _dec.step(_spec_uint);
                new (&_reader_base()) bytes_reader {};
                break;
            case 0x5B:
                throw error("byte strings longer than 2^32-1 bytes are not supported!");
            case 0x5F:
                _dec.step1();
                new (&_reader_base()) chunked_bytes_reader { _dec.push() };
                break;
            case 0x60:
            case 0x61:
            case 0x62:
            case 0x63:
            case 0x64:
            case 0x65:
            case 0x66:
            case 0x67:
            case 0x68:
            case 0x69:
            case 0x6A:
            case 0x6B:
            case 0x6C:
            case 0x6D:
            case 0x6E:
            case 0x6F:
            case 0x70:
            case 0x71:
            case 0x72:
            case 0x73:
            case 0x74:
            case 0x75:
            case 0x76:
            case 0x77:
                _dec.step1();
                _spec_uint = _data_begin[0] & 0x1F;
                _dec.step(_spec_uint);
                new (&_reader_base()) text_reader {};
                break;
            case 0x78:
                _dec.step(2);
                _spec_uint = _data_begin[1];
                _dec.step(_spec_uint);
                new (&_reader_base()) text_reader {};
                break;
            case 0x79:
                _dec.step(3);
                _spec_uint = net_to_host<uint16_t>(*reinterpret_cast<const uint16_t *>(&_data_begin[1]));
                _dec.step(_spec_uint);
                new (&_reader_base()) text_reader {};
                break;
            case 0x7A:
            case 0x7B:
                throw error("strings longer than 65535 bytes are not supported!");
            case 0x7F:
                _dec.step1();
                new (&_reader_base()) chunked_text_reader { _dec.push() };
                break;
            case 0x80:
            case 0x81:
            case 0x82:
            case 0x83:
            case 0x84:
            case 0x85:
            case 0x86:
            case 0x87:
            case 0x88:
            case 0x89:
            case 0x8A:
            case 0x8B:
            case 0x8C:
            case 0x8D:
            case 0x8E:
            case 0x8F:
            case 0x90:
            case 0x91:
            case 0x92:
            case 0x93:
            case 0x94:
            case 0x95:
            case 0x96:
            case 0x97:
                _spec_uint = _data_begin[0] & 0x1F;
                _dec.step1();
                new (&_reader_base()) array_reader_sized { _dec.push() };
                break;
            case 0x98:
                _dec.step(2);
                _spec_uint = _data_begin[1];
                new (&_reader_base()) array_reader_sized { _dec.push() };
                break;
            case 0x99:
                _dec.step(3);
                _spec_uint = net_to_host<uint16_t>(*reinterpret_cast<const uint16_t *>(&_data_begin[1]));
                new (&_reader_base()) array_reader_sized { _dec.push() };
                break;
            case 0x9A:
                _dec.step(5);
                _spec_uint = net_to_host<uint32_t>(*reinterpret_cast<const uint32_t *>(&_data_begin[1]));
                new (&_reader_base()) array_reader_sized { _dec.push() };
                break;
            case 0x9B:
                throw error("array larger than 2^32 - 1 elements are not supported!");
            case 0x9F:
                _dec.step1();
                new (&_reader_base()) array_reader { _dec.push() };
                break;
            case 0xA0:
            case 0xA1:
            case 0xA2:
            case 0xA3:
            case 0xA4:
            case 0xA5:
            case 0xA6:
            case 0xA7:
            case 0xA8:
            case 0xA9:
            case 0xAA:
            case 0xAB:
            case 0xAC:
            case 0xAD:
            case 0xAE:
            case 0xAF:
            case 0xB0:
            case 0xB1:
            case 0xB2:
            case 0xB3:
            case 0xB4:
            case 0xB5:
            case 0xB6:
            case 0xB7:
                _spec_uint = _data_begin[0] & 0x1F;
                _dec.step1();
                new (&_reader_base()) map_reader_sized { _dec.push() };
                break;
            case 0xB8:
                _dec.step(2);
                _spec_uint = _data_begin[1];
                new (&_reader_base()) map_reader_sized { _dec.push() };
                break;
            case 0xB9:
                _dec.step(3);
                _spec_uint = net_to_host<uint16_t>(*reinterpret_cast<const uint16_t *>(&_data_begin[1]));
                new (&_reader_base()) map_reader_sized { _dec.push() };
                break;
            case 0xBA:
                _dec.step(5);
                _spec_uint = net_to_host<uint32_t>(*reinterpret_cast<const uint32_t *>(&_data_begin[1]));
                new (&_reader_base()) map_reader_sized { _dec.push() };
                break;
            case 0xBB:
                throw error("maps larger than 2^32 - 1 elements are not supported!");
            case 0xBF:
                _dec.step1();
                new (&_reader_base()) map_reader { _dec.push() };
                break;
            case 0xC0:
            case 0xC1:
            case 0xC2:
            case 0xC3:
            case 0xC4:
            case 0xC5:
            case 0xC6:
            case 0xC7:
            case 0xC8:
            case 0xC9:
            case 0xCA:
            case 0xCB:
            case 0xCC:
            case 0xCD:
            case 0xCE:
            case 0xCF:
            case 0xD0:
            case 0xD1:
            case 0xD2:
            case 0xD3:
            case 0xD4:
            case 0xD5:
            case 0xD6:
            case 0xD7:
                _spec_uint = _data_begin[0] & 0x1F;
                _dec.step1();
                new (&_reader_base()) tag_reader { _dec.push() };
                break;
            case 0xD8:
                _dec.step(2);
                _spec_uint = _data_begin[1];
                new (&_reader_base()) tag_reader { _dec.push() };
                break;
            case 0xD9:
                _dec.step(3);
                _spec_uint = net_to_host<uint16_t>(*reinterpret_cast<const uint16_t *>(&_data_begin[1]));
                new (&_reader_base()) tag_reader { _dec.push() };
                break;
            case 0xDA:
            case 0xDB:
                throw error("tags with body larger than 65535 bytes are not supported!");
            case 0xF4:
            case 0xF5:
            case 0xF6:
            case 0xF7:
                new (&_reader_base()) special_reader {};
                _dec.step1();
                break;
            case 0xF8:
                throw error(fmt::format("throw: one-byte simple values are not supported!"));
            case 0xF9:
                throw error(fmt::format("throw: two-byte simple values are not supported!"));
            case 0xFA:
                new (&_reader_base()) float32_reader {};
                _dec.step(5);
                break;
            case 0xFB:
                throw error(fmt::format("throw: eight-byte simple values are not supported!"));
            case 0xFF:
                new (&_reader_base()) special_reader {};
                _dec.step1();
                break;
            [[unlikely]] default:
                throw error(fmt::format("an unsupported first byte of a CBOR value: #{:02X}!", _data_begin[0]));
        }
    }

    inline const uint8_t *value::end()
    {
        finalize();
        return _dec.next();
    }

    inline void value::finalize()
    {
        _reader_base().consume();
    }

    inline void value::_mark_end()
    {
        new (&_reader_base()) consumed_reader {};
    }

    inline value &value::at(const size_t pos)
    {
        return array().skip(pos).read();
    }

    inline buffer value::data_special() const
    {
        const auto *special_ptr = _data_begin + special_bytes(special_tag());
        return { special_ptr, _spec_uint };
    }

    inline buffer value::data_raw_ext() const
    {
        return { _data_begin, static_cast<size_t>(_dec.end() - _data_begin) };
    }

    inline const value &reader::_parent(const void *self)
    {
        return reinterpret_cast<const value &>(*(static_cast<const char *>(self) - value::reader_offset));
    }

    inline value *reader::next_chunk(value &lv)
    {
        if (!_parent(this)._dec.done(lv)) {
            if (*_parent(this)._dec.next() != 0xFF)
                return &_parent(this)._dec.read(lv);
        }
        return nullptr;
    }

    inline special_val special_reader::read()
    {
        return _parent(this).special_tag();
    }

    inline void simple_reader::consume()
    {
        _parent(this)._mark_end();
    }

    inline uint64_t uint_reader::read()
    {
        return _parent(this).special_uint();
    }

    inline float float32_reader::read()
    {
        return _parent(this).data_raw_ext().subbuf(1, 4).to_host<float>();
    }

    inline uint64_t nint_reader::read_raw()
    {
        return _parent(this).special_uint();
    }

    inline uint64_t nint_reader::read()
    {
        const auto val = read_raw();
        if (val < std::numeric_limits<uint64_t>::max()) [[likely]]
            return val + 1;
        throw error("a NINT value is too large to be represented as 64-bit unsigned int");
    }

    inline std::string_view text_reader::read()
    {
        return static_cast<std::string_view>(_parent(this).data_special());
    }

    inline void text_reader::read(std::pmr::string &s)
    {
        s = read();
    }

    inline void text_reader::read(std::string &s)
    {
        s = read();
    }

    inline std::string_view chunked_text_reader::read()
    {
        throw error("a chunked string cannot be represented as a single buffer!");
    }

    inline void chunked_text_reader::read(std::pmr::string &s)
    {
        s.clear();
        for (size_t i = 0; i < 1024; ++i) {
            auto *chunk = next_chunk(_dec_level);
            if (!chunk)
                return;
            const auto bytes = chunk->data_special();
            s.insert(s.end(), bytes.begin(), bytes.end());
        }
        throw error("CBOR chunked values may not have more than 1024 chunks!");
    }

    inline void chunked_text_reader::read(std::string &s)
    {
        s.clear();
        for (size_t i = 0; i < 1024; ++i) {
            auto *chunk = next_chunk(_dec_level);
            if (!chunk)
                return;
            const auto bytes = chunk->data_special();
            s.insert(s.end(), bytes.begin(), bytes.end());
        }
        throw error("CBOR chunked values may not have more than 1024 chunks!");
    }

    inline void chunked_text_reader::consume()
    {
        for (size_t i = 0; i < 1024; ++i) {
            if (const auto *chunk = next_chunk(_dec_level); !chunk) {
                _parent(this)._dec.step(1);
                _parent(this)._dec.pop();
                _parent(this)._mark_end();
                return;
            }
        }
        throw error("CBOR chunked values may not have more than 1024 chunks!");
    }

    inline buffer bytes_reader::read()
    {
        return _parent(this).data_special();
    }

    inline void bytes_reader::read(std::pmr::vector<uint8_t> &b)
    {
        const auto bytes = read();
        b.resize(bytes.size());
        memcpy(b.data(), bytes.data(), bytes.size());
    }

    inline void bytes_reader::read(write_vector &b)
    {
        b = read();
    }

    inline buffer chunked_bytes_reader::read()
    {
        throw error("a chunked bytestring cannot be represented as a single buffer!");
    }

    inline void chunked_bytes_reader::read(std::pmr::vector<uint8_t> &b)
    {
        b.clear();
        for (size_t i = 0; i < 1024; ++i) {
            auto *chunk = next_chunk(_dec_level);
            if (!chunk)
                return;
            const auto bytes = chunk->data_special();
            b << bytes;
        }
        throw error("CBOR chunked values may not have more than 1024 chunks!");
    }

    inline void chunked_bytes_reader::read(write_vector &b)
    {
        b.clear();
        for (size_t i = 0; i < 1024; ++i) {
            auto *chunk = next_chunk(_dec_level);
            if (!chunk)
                return;
            const auto bytes = chunk->data_special();
            b << bytes;
        }
        throw error("CBOR chunked values may not have more than 1024 chunks!");
    }

    inline void chunked_bytes_reader::consume()
    {
        for (size_t i = 0; i < 1024; ++i) {
            if (const auto *chunk = next_chunk(_dec_level); !chunk) {
                _parent(this)._dec.step(1);
                _parent(this)._dec.pop();
                _parent(this)._mark_end();
                return;
            }
        }
        throw error("CBOR chunked values may not have more than 1024 chunks!");
    }

    inline tag_reader::tag_reader(value &dec_level):
        _begin_ptr { _parent(this)._dec.next() },
        _val { _parent(this)._dec.read(dec_level) }
    {
    }

    inline void tag_reader::consume()
    {
        _val.finalize();
        _parent(this)._dec.pop();
        _parent(this)._mark_end();
    }

    inline uint64_t tag_reader::id() const noexcept
    {
        return _parent(this).special_uint();
    }

    inline value &tag_reader::read()
    {
        return _val;
    }

    inline bool array_reader::done()
    {
        if (!_parent(this)._dec.done(_dec_level)) {
            if (*_parent(this)._dec.next() != 0xFF)
                return false;
            // the 0xFF byte is not consumed here so that repeated calls to done return the same result
        }
        return true;
    }

    inline value &array_reader::read()
    {
        return _parent(this)._dec.read(_dec_level);
    }

    inline array_reader &array_reader::skip(const size_t num_items)
    {
        for (size_t i = 0; i < num_items; ++i) {
            read();
        }
        return *this;
    }

    inline void array_reader::consume()
    {
        while (!done()) {
            read();
        }
        _parent(this)._dec.step(1);
        _parent(this)._dec.pop();
        _parent(this)._mark_end();
    }

    inline bool array_reader_sized::done()
    {
        if (!_parent(this)._dec.done(_dec_level)) {
            if (_pos < _parent(this).special_uint())
                return false;
        }
        return true;
    }

    inline value &array_reader_sized::read()
    {
        ++_pos;
        return _parent(this)._dec.read(_dec_level);
    }

    inline void array_reader_sized::consume()
    {
        while (!done()) {
            read();
        }
        _parent(this)._dec.pop();
        _parent(this)._mark_end();
    }

    inline bool map_reader::done()
    {
        if (!_parent(this)._dec.done(_dec_level)) {
            if (*_parent(this)._dec.next() != 0xFF)
                return false;
            // the 0xFF byte is not consumed here so that repeated calls to done return the same result
        }
        return true;
    }

    inline value &map_reader::read_key()
    {
        return _parent(this)._dec.read(_dec_level);
    }

    // the move argument is just a reminder that the previously returned key argument will be overwritten
    inline value &map_reader::read_val(value &&)
    {
        return _parent(this)._dec.read(_dec_level);
    }

    inline void map_reader::consume()
    {
        while (!done()) {
            auto &key = read_key();
            read_val(std::move(key));
        }
        _parent(this)._dec.step(1);
        _parent(this)._dec.pop();
        _parent(this)._mark_end();
    }

    inline void map_reader::skip(const size_t num_items)
    {
        for (size_t i = 0; i < num_items; ++i) {
            auto &key = read_key();
            read_val(std::move(key));
        }
    }

    inline bool map_reader_sized::done()
    {
        if (!_parent(this)._dec.done(_dec_level)) {
            if (_pos < _parent(this).special_uint())
                return false;
        }
        return true;
    }

    inline void map_reader_sized::consume()
    {
        while (!done()) {
            auto &key = read_key();
            read_val(std::move(key));
        }
        _parent(this)._dec.pop();
        _parent(this)._mark_end();
    }

    inline value &map_reader_sized::read_key()
    {
        return _parent(this)._dec.read(_dec_level);
    }

    inline value &map_reader_sized::read_val(value &&)
    {
        ++_pos;
        return _parent(this)._dec.read(_dec_level);
    }

    template<typename OUT_IT>
    OUT_IT format_to(OUT_IT out_it, value &v, const size_t depth, const size_t max_seq_to_expand)
    {
        switch (v.type()) {
            case major_type::uint: return fmt::format_to(out_it, "I {}", v.uint());
            case major_type::nint: return fmt::format_to(out_it, "I -{}", v.nint());
            case major_type::bytes: {
                std::optional<write_vector> storage {};
                std::optional<buffer> b {};
                if (!v.indefinite()) [[likely]] {
                    b = v.bytes();
                } else {
                    storage.emplace();
                    v.to_bytes(*storage);
                    b = *storage;
                }
                if (cbor::is_ascii(*b))
                    return fmt::format_to(out_it, "B {}#{} ('{}')", v.indefinite() ? "chunked " : "", b, static_cast<std::string_view>(*b));
                return fmt::format_to(out_it, "B {}#{}", v.indefinite() ? "chunked " : "", *b);
            }
            case major_type::text: {
                std::optional<std::string> storage {};
                std::string_view s;
                if (!v.indefinite()) [[likely]] {
                    s = v.text();
                } else {
                    storage.emplace();
                    v.to_text(*storage);
                    s = *storage;
                }
                return fmt::format_to(out_it, "T {}'{}'", v.indefinite() ? "chunked " : "", s);
            }
            case major_type::array: {
                auto &it = v.array();
                out_it = fmt::format_to(out_it, "[");
                size_t i = 0;
                if (!it.done()) {
                    out_it = fmt::format_to(out_it, "\n");
                    for (; !it.done(); ++i) {
                        auto &av = it.read();
                        if (i < max_seq_to_expand) {
                            out_it = fmt::format_to(out_it, "{:{}}    #{}: ", "", depth * 4, i);
                            out_it = format_to(out_it, av, depth + 1, max_seq_to_expand);
                            out_it = fmt::format_to(out_it, "\n");
                        }
                    }
                    if (i > max_seq_to_expand)
                        out_it = fmt::format_to(out_it, "{:{}}    ...\n", "", depth * 4);
                    out_it = fmt::format_to(out_it, "{:{}}", "", depth * 4);
                }
                return fmt::format_to(out_it, "]({}size: {})", v.indefinite() ? "unbounded " : "", i);
            }
            case major_type::map: {
                auto &it = v.map();
                out_it = fmt::format_to(out_it, "{{");
                size_t i = 0;
                if (!it.done()) {
                    out_it = fmt::format_to(out_it, "\n");
                    for (; !it.done(); ++i) {
                        auto &key = it.read_key();
                        if (i < max_seq_to_expand) {
                            out_it = fmt::format_to(out_it, "{:{}}    #{}: ", "", depth * 4, i);
                            out_it = format_to(out_it, key, depth + 1, max_seq_to_expand);
                            out_it = fmt::format_to(out_it, ": ");
                        }
                        auto &val = it.read_val(std::move(key));
                        if (i < max_seq_to_expand) {
                            out_it = format_to(out_it, val, depth + 1, max_seq_to_expand);
                            out_it = fmt::format_to(out_it, "\n");
                        }
                    }
                    if (i > max_seq_to_expand)
                        out_it = fmt::format_to(out_it, "{:{}}    ...\n", "", depth * 4);
                    out_it = fmt::format_to(out_it, "{:{}}", "", depth * 4);
                }
                return fmt::format_to(out_it, "}}({}size: {})", v.indefinite() ? "unbounded " : "", i);
            }
            case major_type::tag: {
                auto &t = v.tag();
                out_it = fmt::format_to(out_it, "TAG {} ", t.id());
                out_it = format_to(out_it, t.read(), depth, max_seq_to_expand);
                return out_it;
            }
            case major_type::simple:
                if (v.special_tag() == special_val::four_bytes)
                    return fmt::format_to(out_it, "F32 {}", v.float32());
                return fmt::format_to(out_it, "{}", v.special());
            [[unlikely]] default:
                throw error(fmt::format("unsupported CBOR type {}", v.type()));
        }
    }

    extern template std::ostreambuf_iterator<char> format_to(std::ostreambuf_iterator<char> out_it, value &v, const size_t depth, const size_t max_seq_to_expand);
    extern template std::back_insert_iterator<std::string> format_to(std::back_insert_iterator<std::string> out_it, value &v, const size_t depth, const size_t max_seq_to_expand);

    inline std::string value::to_string(const size_t max_seq_to_expand)
    {
        std::string res {};
        format_to(std::back_inserter(res), *this, 0, max_seq_to_expand);
        return res;
    }

    inline void value::to_stream(std::ostream &os, const size_t max_seq_to_expand)
    {
        format_to(std::ostreambuf_iterator<char>(os), *this, 0, max_seq_to_expand);
    }

    struct parsed_value {
        parsed_value(const buffer bytes):
            _dec { bytes },
            _val { _dec.read() }
        {
        }

        value &get()
        {
            return _val;
        }
    private:
        decoder _dec;
        value &_val;
    };

    inline value &extract(value &v, const std::span<const size_t> path, const size_t idx=0)
    {
        if (idx < path.size()) [[likely]] {
            switch (const auto typ = v.type(); typ) {
                case major_type::array: {
                    auto &it = v.array();
                    it.skip(path[idx]);
                    auto &next_v = it.read();
                    return extract(next_v, path, idx + 1);
                }
                case major_type::map: {
                    auto &it = v.map();
                    it.skip(path[idx]);
                    auto &next_k = it.read_key();
                    auto &next_v = it.read_val(std::move(next_k));
                    return extract(next_v, path, idx + 1);
                }
                [[unlikely]] default:
                    throw error(fmt::format("an unexpected element type in the extract path: {}", typ));
            }
        }
        return v;
    }

    inline parsed_value parse(const buffer data)
    {
        return { data };
    }
}

namespace fmt {
    template<>
    struct formatter<daedalus_turbo::cbor::zero2::value>: formatter<int> {
        template<typename FormatContext>
        auto format(auto &v, FormatContext &ctx) -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", v.to_string());
        }
    };

    template<>
    struct formatter<daedalus_turbo::cbor::zero2::parsed_value>: formatter<int> {
        template<typename FormatContext>
        auto format(auto &v, FormatContext &ctx) -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", v.get());
        }
    };
}

#endif // !DAEDALUS_TURBO_CBOR_ZERO2_HPP