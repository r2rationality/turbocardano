/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2024 Alex Sierkov (alex dot sierkov at gmail dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */
#ifndef DAEDALUS_TURBO_CBOR_HPP
#define DAEDALUS_TURBO_CBOR_HPP

#include <span>
#include <stdexcept>
#include <memory>
#include <source_location>
#include <string>
#include <variant>
#include <dt/container.hpp>
#include <dt/util.hpp>

namespace daedalus_turbo {
    namespace cbor {
        static constexpr size_t default_max_collection_size = 0x100'000;

        typedef daedalus_turbo::error error;
        struct collection_too_big_error: error {
            explicit collection_too_big_error(const size_t size):
                error { fmt::format("Trying to create an array or map larger than {} items", size), my_stacktrace() }
            {
            }
        };
        // inherit directly from std::runtime_error to save on the expensive stackstrace creation
        struct incomplete_error: std::runtime_error {
            explicit incomplete_error(): std::runtime_error { "CBOR value extends beyond the end of stream" }
            {
            }
        };
    }

    using cbor_error = cbor::error;
    using cbor_incomplete_data_error = cbor::incomplete_error;

    using cbor_buffer = buffer;
    struct cbor_value;

    typedef std::pair<cbor_value, cbor_value> cbor_map_value;
    typedef vector<cbor_map_value> cbor_map;

    struct cbor_array: vector<cbor_value>
    {
        inline const cbor_value &at(size_t pos, const std::source_location &loc=std::source_location::current()) const;
    };

    enum cbor_value_type {
        CBOR_UINT,
        CBOR_NINT,
        CBOR_BYTES,
        CBOR_TEXT,
        CBOR_ARRAY,
        CBOR_MAP,
        CBOR_TAG,
        CBOR_SIMPLE_TRUE,
        CBOR_SIMPLE_NULL,
        CBOR_SIMPLE_UNDEFINED,
        CBOR_SIMPLE_BREAK,
        CBOR_SIMPLE_FALSE,
        CBOR_FLOAT16,
        CBOR_FLOAT32,
        CBOR_FLOAT64
    };

    typedef std::pair<uint64_t, std::unique_ptr<cbor_value>> cbor_tag;

    typedef std::variant<
            uint64_t,
            float,
            cbor_buffer,
            cbor_array,
            cbor_map,
            cbor_tag
        > cbor_value_content;

    struct cbor_value {
        const uint8_t *data;
        size_t size;
        cbor_value_type type;
        std::unique_ptr<uint8_vector> storage = nullptr;

        cbor_value()
        {
        }

        static std::string &type_name(enum cbor_value_type type)
        {
            static std::array<std::string, 15> names {
                "unsigned integer", "negative integer", "bytes", "text",
                "array", "map", "tag", "true",
                "null", "undefined", "break", "false",
                "float16", "float32", "float64"
            };
            size_t type_idx = static_cast<size_t>(type);
            if (type_idx >= names.size())
                throw error("unsupported CBOR type index: {}", type_idx);
            return names[type_idx];
        }

        const std::string &type_name() const
        {
            return type_name(type);
        }   

        const cbor_buffer data_buf() const {
            return cbor_buffer(data, size);
        }

        inline bool operator<(const cbor_value &aVal) const {
            size_t minSize = size;
            if (aVal.size < minSize) minSize = aVal.size;
            int res = memcmp(data, aVal.data, minSize);
            if (res == 0) return size < aVal.size;
            return res < 0;
        }

        inline uint64_t uint(const std::source_location &loc = std::source_location::current()) const
        {
            return get<uint64_t>(CBOR_UINT, loc);
        }

        inline uint64_t nint(const std::source_location &loc = std::source_location::current()) const
        {
            return get<uint64_t>(CBOR_NINT, loc) + 1;
        }

        inline float float32(const std::source_location &loc = std::source_location::current()) const {
            return get<float>(CBOR_FLOAT32, loc);
        }

        inline const cbor_buffer &buf(const std::source_location &loc = std::source_location::current()) const
        {
            return get_ref<cbor_buffer>(CBOR_BYTES, loc);
        }

        inline std::string_view text(const std::source_location &loc = std::source_location::current()) const
        {
            const auto &buf = get_ref<cbor_buffer>(CBOR_TEXT, loc);
            return { reinterpret_cast<const char *>(buf.data()), buf.size() };
        }

        inline const buffer &span(const std::source_location &loc = std::source_location::current()) const
        {
            return  get_ref<cbor_buffer>(CBOR_BYTES, loc);
        }

        inline const cbor_array &array(const std::source_location &loc = std::source_location::current()) const
        {
            return get_ref<cbor_array>(CBOR_ARRAY, loc);
        }

        inline const cbor_map &map(const std::source_location &loc = std::source_location::current()) const
        {
            return get_ref<cbor_map>(CBOR_MAP, loc);
        }

        inline const cbor_tag &tag(const std::source_location &loc = std::source_location::current()) const
        {
            return get_ref<cbor_tag>(CBOR_TAG, loc);
        }

        inline size_t offset(const uint8_t *base) const noexcept
        {
            return data - base;
        }

        template<typename T>
        inline void set_content(T &&val)
        {
            content = std::move(val);
        }

        inline const std::span<const uint8_t> raw_span() const
        {
            return std::span<const uint8_t>(data, size);
        }

    private:

        template<typename T>
        inline const T get(cbor_value_type exp_type, const std::source_location &loc = std::source_location::current()) const
        {
            try {
                return std::get<T>(content);
            } catch (std::bad_variant_access &ex) {
                throw cbor_error("invalid cbor value access, expecting type {} while the present type is {} in file {} line {}!",
                                 type_name(exp_type), type_name(), loc.file_name(), loc.line());
            }
        }

        template<typename T>
        inline const T &get_ref(cbor_value_type exp_type, const std::source_location &loc = std::source_location::current()) const
        {
            try {
                return std::get<T>(content);
            } catch (std::bad_variant_access &ex) {
                throw cbor_error("invalid cbor value access, expecting type {} while the present type is {} in file {} line {}!",
                                 type_name(exp_type), type_name(), loc.file_name(), loc.line());
            }
        }

        cbor_value_content content;
    };

    inline const cbor_value &cbor_array::at(size_t pos, const std::source_location &loc) const
    {
        try {
            return vector::at(pos);
        } catch (std::out_of_range &ex) {
            throw cbor_error("invalid element index {} in the array of size {} in file {} line {}!",
                                pos, size(), loc.file_name(), loc.line());
        }
    }

    class cbor_parser {
        const uint8_t *_data;
        const size_t _size;
        size_t _offset = 0;

        void _read_unsigned_int(cbor_value &val, uint8_t augVal, const cbor_buffer &augBuf) {
            uint64_t x = 0;
            if (augBuf.size() > 0) {
                for (size_t i = 0; i < augBuf.size(); ++i) {
                    x <<= 8;
                    x |= augBuf.data()[i];
                }
            } else {
                x = augVal;
            }
            val.type = CBOR_UINT;
            val.set_content(x);
        }

        void _read_negative_int(cbor_value &val, uint8_t augVal, const cbor_buffer &augBuf) {
            _read_unsigned_int(val, augVal, augBuf);
            val.type = CBOR_NINT;
        }

        void _read_byte_string(cbor_value &val, uint8_t augVal, const cbor_buffer &augBuf, bool indefinite) {
            val.type = CBOR_BYTES;
            if (!indefinite) {
                cbor_value size;
                _read_unsigned_int(size, augVal, augBuf);
                const size_t string_size = size.uint();
                if (string_size > _max_collection_size)
                    throw cbor::collection_too_big_error { _max_collection_size };
                if (_offset + string_size > _size)
                    throw cbor_incomplete_data_error();
                val.set_content(cbor_buffer(&_data[_offset], string_size));
                _offset += string_size;
            } else {
                std::unique_ptr<uint8_vector> storage = std::make_unique<uint8_vector>();
                cbor_value chunk;
                for (;;) {
                    read(chunk);
                    if (chunk.type == CBOR_SIMPLE_BREAK)
                        break;
                    if (chunk.type != val.type)
                        throw cbor_error("badly encoded indefinite byte string!");
                    const cbor_buffer &chunk_buf = chunk.buf();
                    const size_t chunk_off = storage->size();
                    const auto new_size = storage->size() + chunk_buf.size();
                    if (new_size > _max_collection_size)
                        throw cbor::collection_too_big_error { _max_collection_size };
                    storage->resize(new_size);
                    memcpy(storage->data() + chunk_off, chunk_buf.data(), chunk_buf.size());
                }
                storage->shrink_to_fit();
                val.set_content(buffer(storage->data(), storage->size()));
                val.storage = std::move(storage);
            }
        }

        void _read_text_string(cbor_value &val, uint8_t augVal, const cbor_buffer &augBuf, bool indefinite) {
            if (indefinite) throw cbor_error("indefinite text strings are not supported yet");
            cbor_value size;
            _read_unsigned_int(size, augVal, augBuf);
            val.type = CBOR_TEXT;
            const size_t string_size = size.uint();
            if (_offset + string_size > _size)
                throw cbor_incomplete_data_error();
            val.set_content(cbor_buffer { &_data[_offset], string_size });
            _offset += string_size;
        }

        void _read_array(cbor_value &val, uint8_t augVal, const cbor_buffer &augBuf, bool indefinite) {
            cbor_array items {};
            if (indefinite) {
                cbor_value item;
                for (;;) {
                    read(item);
                    if (item.type == CBOR_SIMPLE_BREAK)
                        break;
                    if (items.size() >= _max_collection_size)
                        throw cbor::collection_too_big_error { _max_collection_size };
                    items.emplace_back(std::move(item));
                }
            } else {
                cbor_value size;
                _read_unsigned_int(size, augVal, augBuf);
                const size_t array_size = size.uint();
                if (array_size > _max_collection_size)
                    throw cbor::collection_too_big_error { _max_collection_size };
                items.resize(array_size);
                for (size_t i = 0; i < array_size; ++i) {
                    read(items[i]);
                }
            }
            val.type = CBOR_ARRAY;
            val.set_content(std::move(items));
        }

        void _read_map(cbor_value &val, uint8_t augVal, const cbor_buffer &augBuf, bool indefinite) {
            cbor_map map {};
            if (indefinite) {
                cbor_value itemKey, itemValue;
                for (;;) {
                    if (map.size() >= _max_collection_size)
                        throw cbor::collection_too_big_error { _max_collection_size };
                    read(itemKey);
                    if (itemKey.type == CBOR_SIMPLE_BREAK) break;
                    read(itemValue);
                    map.emplace_back(std::move(itemKey), std::move(itemValue));
                }
            } else {
                cbor_value size;
                _read_unsigned_int(size, augVal, augBuf);
                const size_t map_size = size.uint();
                if (map_size >= _max_collection_size)
                    throw cbor::collection_too_big_error { _max_collection_size };
                map.resize(map_size);
                for (size_t i = 0; i < map_size; ++i) {
                    read(map[i].first);
                    read(map[i].second);
                }
            }
            val.type = CBOR_MAP;
            val.set_content(std::move(map));
        }

        void _read_tagged_value(cbor_value &val, uint8_t augVal, const cbor_buffer &augBuf) {
            cbor_value tag;
            _read_unsigned_int(tag, augVal, augBuf);
            std::unique_ptr<cbor_value> item(new cbor_value());
            read(*item);
            val.type = CBOR_TAG;
            cbor_tag new_tag(tag.uint(), std::move(item));
            val.set_content(std::move(new_tag));
        }

        void _read_float32(cbor_value &val, uint8_t /*augVal*/, const cbor_buffer &augBuf)
        {
            static_assert(sizeof(float) == 4);
            if (augBuf.size() != 4) throw cbor_error("a float32 value with aug buffer size != 4!");
            val.type = CBOR_FLOAT32;
            uint8_t local_order[sizeof(float)];
            for (size_t i = 0; i < augBuf.size(); ++i)
                local_order[i] = augBuf.data()[augBuf.size() - 1 - i];
            float tmp;
            memcpy(&tmp, local_order, sizeof(tmp));
            val.set_content(std::move(tmp));
        }

        void _read_simple_value(cbor_value &val, uint8_t augVal, const cbor_buffer &augBuf, bool) {
            switch (augVal) {
                case 20:
                    val.type = CBOR_SIMPLE_FALSE;
                    break;

                case 21:
                    val.type = CBOR_SIMPLE_TRUE;
                    break;

                case 22:
                    val.type = CBOR_SIMPLE_NULL;
                    break;

                case 23:
                    val.type = CBOR_SIMPLE_UNDEFINED;
                    break;

                case 26:
                    _read_float32(val, augVal, augBuf);
                    break;

                case 31:
                    val.type = CBOR_SIMPLE_BREAK;
                    break;

                default:
                    throw cbor_error("simple values beyond BREAK are not supported yet! augVal: {}, augBuf.size: {}", (int)augVal, augBuf.size());
            }            
        }

    public:
        const size_t _max_collection_size = cbor::default_max_collection_size;

        explicit cbor_parser(const buffer &buf): _data(buf.data()), _size(buf.size())
        {
        }

        void read(cbor_value &val) {
            if (_offset + 1 > _size) throw cbor_incomplete_data_error();
            val.data = &_data[_offset];
            uint8_t hdr = _data[_offset++];
            uint8_t type = (hdr >> 5) & 0x7;
            uint8_t augVal = hdr & 0x1F;
            bool indefinite = false;
            cbor_buffer augBuf {};

            switch (augVal) {
                case 24:
                    if (_offset + 1 > _size) throw cbor_incomplete_data_error();
                    augBuf = buffer(&_data[_offset], 1);
                    _offset += 1;
                    break;

                case 25:
                    if (_offset + 2 > _size) throw cbor_incomplete_data_error();
                    augBuf = buffer(&_data[_offset], 2);
                    _offset += 2;
                    break;

                case 26:
                    if (_offset + 4 > _size) throw cbor_incomplete_data_error();
                    augBuf = buffer(&_data[_offset], 4);
                    _offset += 4;
                    break;

                case 27:
                    if (_offset + 8 > _size) throw cbor_incomplete_data_error();
                    augBuf = buffer(&_data[_offset], 8);
                    _offset += 8;
                    break;

                case 28:
                case 29:
                case 30:
                    throw cbor_error("Invalid CBOR header argument value!");

                case 31:
                    if (type == 0 || type == 1 || type == 6) throw cbor_error("Invalid CBOR header: unexpected indefinite value");
                    indefinite = true;
                    break;

                default:
                    if (augVal >= 24) throw cbor_error("Internal error: reached an impossible state!");
                    break;
            }

            switch (type) {
                case 0:
                    _read_unsigned_int(val, augVal, augBuf);
                    break;

                case 1:
                    _read_negative_int(val, augVal, augBuf);
                    break;

                case 2:
                    _read_byte_string(val, augVal, augBuf, indefinite);
                    break;
                
                case 3:
                    _read_text_string(val, augVal, augBuf, indefinite);
                    break;

                case 4:
                    _read_array(val, augVal, augBuf, indefinite);
                    break;

                case 5:
                    _read_map(val, augVal, augBuf, indefinite);
                    break;

                case 6: 
                    _read_tagged_value(val, augVal, augBuf);
                    break;

                case 7:
                    _read_simple_value(val, augVal, augBuf, indefinite);
                    break;

                default: throw cbor_error("Internal error: reached an impossible state!");
            }
            val.size = _offset - val.offset(_data);
        }

        bool eof() const {
            return _offset >= _size;
        }

        size_t offset() const {
            return _offset;
        }

    };

    inline std::vector<size_t> parse_value_path(const std::string_view text)
    {
        std::vector<size_t> value_path;
        std::string_view text_path { text };
        while (text_path.size() > 0) {
            size_t next_pos = text_path.find('.');
            std::string idx_text;
            if (next_pos != std::string_view::npos) {
                idx_text = text_path.substr(0, next_pos);
                text_path = text_path.substr(next_pos + 1);
            } else {
                idx_text = text_path;
                text_path = text_path.substr(0, 0);
            }
            size_t idx = std::stoull(idx_text);
            value_path.push_back(idx);
        }
        return value_path;
    }

    inline const cbor_value &extract_value(const cbor_value &v, const std::span<size_t> &path, size_t idx=0)
    {
        if (idx >= path.size()) return v;
        if (v.type != CBOR_ARRAY) throw cbor_error("at path index {}: value must be an array but got CBOR type: {}!", idx, (unsigned)v.type);
        const cbor_array &a = v.array();
        if (a.size() <= path[idx]) throw cbor_error("at path index {}: requested index {} but got an array of size {} only!", idx, path[idx], a.size());
        return extract_value(a[path[idx]], path, idx + 1);
    }

    inline bool is_ascii(const buffer &b)
    {
        for (const uint8_t *p = b.data(), *end = p + b.size(); p < end; ++p) {
            if (*p < 32 || *p > 127) return false;
        }
        return true;
    }

    inline void print_cbor_value(std::ostream &os, const cbor_value &val, const cbor_value &base, const size_t max_depth=0, const size_t depth = 0, const size_t max_list_to_expand=10)
    {
        std::string shift_str = "";
        for (size_t i = 0; i < depth * 4; ++i) shift_str += ' ';
        switch (val.type) {
            case CBOR_UINT:
                os << shift_str << "UINT: " << val.uint() << " offset: " << (val.data - base.data) << " size: " << val.size << '\n';
                break;

            case CBOR_NINT:
                os << shift_str << "NINT: -" << val.nint() << " offset: " << (val.data - base.data) << " size: " << val.size << '\n';
                break;

            case CBOR_BYTES: {
                const cbor_buffer &b = val.buf();
                os << shift_str << "BYTES offset: " << (val.data - base.data) << " " << b.size() << " bytes";
                os << " data: " << b;
                if (is_ascii(b)) {
                    const std::string_view sv(reinterpret_cast<const char *>(b.data()), b.size());
                    os << " text: '" << sv << "'";
                }
                os << '\n';
                break;
            }

            case CBOR_TEXT: {
                const cbor_buffer &b = val.buf();
                const std::string_view sv(reinterpret_cast<const char *>(b.data()), b.size());
                os << shift_str << "TEXT offset: " << (val.data - base.data) << " " << b.size() << " bytes";
                if (b.size() <= 64) os << " text: '" << sv << "'";
                else os << " text: '" << sv.substr(0, 64) << "...'";
                os << '\n';
                break;
            }

            case CBOR_ARRAY: {
                const cbor_array &a = val.array();
                os << shift_str << "ARRAY: " << a.size() << " elements, offset: " << (val.data - base.data) << " data size: " << val.size << '\n';
                if ((max_list_to_expand == 0 || a.size() <= max_list_to_expand) && depth < max_depth) {
                    for (size_t i = 0; i < a.size(); ++i) {
                        os << shift_str << "    VAL " << i << ":\n";
                        print_cbor_value(os, a[i], base, max_depth, depth + 2, max_list_to_expand);
                    }
                }
                break;
            }

            case CBOR_MAP: {
                const cbor_map &m = val.map();
                os << shift_str << "MAP: " << m.size() << " elements, offset: " << (val.data - base.data) << " data size: " << val.size << '\n';
                if ((max_list_to_expand == 0 || m.size() <= max_list_to_expand) && depth + 1 < max_depth) {
                    for (size_t i = 0; i < m.size(); ++i) {
                        os << shift_str << "    KEY " << i << ":\n";
                        print_cbor_value(os, m[i].first, base, max_depth, depth + 2, max_list_to_expand);
                        os << shift_str << "    VAL " << i << ":\n";
                        print_cbor_value(os, m[i].second, base, max_depth, depth + 2, max_list_to_expand);
                    }
                }
                break;
            }

            case CBOR_TAG: {
                const cbor_tag &t = val.tag();
                os << shift_str << "TAG: " << t.first << " offset: " << (val.data - base.data) << " data size: " << val.size << '\n';
                if (depth < max_depth) {
                    print_cbor_value(os, *t.second, base, max_depth, depth + 1, max_list_to_expand);
                }
                break;
            }

            case CBOR_SIMPLE_NULL:
                os << shift_str << "NULL" << " offset: " << (val.data - base.data) << '\n';
                break;

            case CBOR_SIMPLE_TRUE:
                os << shift_str << "TRUE" << " offset: " << (val.data - base.data) << '\n';
                break;

            case CBOR_SIMPLE_FALSE:
                os << shift_str << "FALSE" << " offset: " << (val.data - base.data) << '\n';
                break;

            default:
                os << shift_str << "Unsupported CBOR type: " << static_cast<unsigned>(val.type) << '\n';
                break;
        }
    }

    namespace cbor {
        inline std::string stringify(const cbor_value &item)
        {
            std::stringstream ss {};
            print_cbor_value(ss, item, item, 10, 0, 100);
            return ss.str();
        }

        inline std::string stringify(const buffer &raw_data)
        {
            std::stringstream ss {};
            cbor_parser parser { raw_data };
            cbor_value item {};
            for (size_t i = 0; !parser.eof(); ++i) {
                parser.read(item);
                ss << "ITEM " << i << ": " << stringify(item);
            }
            return ss.str();
        }

        inline cbor_value parse(const buffer &raw_data)
        {
            cbor_parser parser { raw_data };
            if (parser.eof())
                throw error("byte stream is empty - can't parse it!");
            cbor_value item {};
            parser.read(item);
            return item;
        }
    }
}

namespace fmt {
    template<>
    struct formatter<daedalus_turbo::cbor_value_type>: public formatter<unsigned> {
    };
}

#endif //!DAEDALUS_TURBO_CBOR_HPP