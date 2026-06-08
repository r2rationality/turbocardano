#pragma once
/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com) */

#include <algorithm>
#include <array>
#include <concepts>
#include <span>
#include <utility>
#include "error.hpp"
#include "format.hpp"

namespace turbo {
    typedef std::span<uint8_t> write_buffer;

    template <std::integral T>
    [[nodiscard]] constexpr T byteswap_if_little_endian(T value) noexcept {
        if constexpr (std::endian::native == std::endian::little) {
            return std::byteswap(value);
        }
        return value;
    }

    template <std::integral T>
    [[nodiscard]] constexpr T host_to_net(T value) noexcept {
        return byteswap_if_little_endian(value);
    }

    template <std::integral T>
    [[nodiscard]] constexpr T net_to_host(T value) noexcept {
        return byteswap_if_little_endian(value);
    }

    struct buffer: std::span<const uint8_t> {
        using base_type = std::span<const uint8_t>;

        buffer() = default;
        buffer(const buffer &) = default;

        template <typename T, size_t SZ> requires (SZ != std::dynamic_extent)
        buffer(const std::span<T, SZ> bytes):
            buffer{reinterpret_cast<const uint8_t *>(bytes.data()), SZ * sizeof(T)}
        {
        }

        template <typename T>
        buffer(const std::span<T> bytes):
            buffer{reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size() * sizeof(T)}
        {
        }

        buffer(const std::string_view s):
        buffer{reinterpret_cast<const uint8_t *>(s.data()), s.size()}
        {
        }

        buffer(const std::string &s):
            buffer{reinterpret_cast<const uint8_t *>(s.data()), s.size()}
        {
        }

        buffer(const uint8_t *data, const size_t sz):
            std::span<const uint8_t>{data, sz}
        {
            if (data == nullptr && sz != 0) [[unlikely]]
                throw error(fmt::format("a buffer cannot have a non-zero size {} with a null data pointer!", sz));
        }

        buffer &operator=(const buffer &o) =default;

        template<typename M>
        static constexpr buffer from(const M &val)
        {
            return buffer { reinterpret_cast<const uint8_t *>(&val), sizeof(val) };
        }

        template<typename M>
        constexpr M to() const
        {
            static_assert(std::is_trivially_copyable_v<M>);
            if (size() != sizeof(M)) [[unlikely]]
                throw error(fmt::format("buffer size: {} does not match the type's size: {}!", size(), sizeof(M)));
            M result;
            std::memcpy(&result, data(), sizeof(M));
            return result;
        }

        template<typename M>
        constexpr M to_host() const
        {
            if (size() != sizeof(M)) [[unlikely]]
                throw error(fmt::format("buffer size: {} does not match the type's size: {}!", size(), sizeof(M)));
            return net_to_host(to<M>());
        }

        operator std::string_view() const noexcept
        {
            return { reinterpret_cast<const char *>(data()), size() };
        }

        std::strong_ordering operator<=>(const buffer &o) const noexcept
        {
            const auto min_sz = std::min(size(), o.size());
            if (min_sz > 0) [[likely]] {
                const auto cmp = memcmp(data(), o.data(), min_sz);
                if (cmp < 0)
                    return std::strong_ordering::less;
                if (cmp > 0)
                    return std::strong_ordering::greater;
            }
            return size() <=> o.size();
        }

        bool operator==(const buffer &o) const noexcept
        {
            return std::strong_ordering::equal == (*this <=> o);
        }

        uint8_t at(const size_t off) const
        {
            if (off < size()) [[likely]]
                return (*this)[off];
            throw error(fmt::format("requested offset: {} that behind the end of buffer: {}!", off, size()));
        }

        buffer subbuf(const size_t offset, const size_t sz) const
        {
            if (static_cast<int>(offset <= size()) & static_cast<int>(sz <= size() - offset)) [[likely]]
                return buffer { data() + offset, sz };
            throw error(fmt::format("requested offset: {} and size: {} end over the end of buffer's size: {}!", offset, sz, size()));
        }

        buffer subbuf(const size_t offset) const
        {
            if (offset <= size()) [[likely]]
                return subbuf(offset, size() - offset);
            throw error(fmt::format("a buffer's offset {} is greater than its size {}", offset, size()));
        }
    };

    template<size_t SZ>
    struct
    byte_array: std::array<uint8_t, SZ> {
        using base_type = std::array<uint8_t, SZ>;
        using base_type::base_type;

        template<typename C=byte_array<SZ>>
        static C from_hex(const std::string_view hex)
        {
            C data;
            init_from_hex(data, hex);
            return data;
        }

        byte_array() =default;

        byte_array(const std::initializer_list<uint8_t> s) {
            if (s.size() != SZ) [[unlikely]]
                throw error(fmt::format("span must be of size {} but got {}", SZ, s.size()));
            size_t i = 0;
            for (const auto b: s)
                *(base_type::data() + i++) = b;
        }

        byte_array(const buffer s)
        {
            if (s.size() != SZ) [[unlikely]]
                throw error(fmt::format("string_view must be of size {} but got {}", SZ, s.size()));
            memcpy(this, std::data(s), SZ);
        }

        byte_array(const std::string_view s)
        {
            if (s.size() != SZ) [[unlikely]]
                throw error(fmt::format("string_view must be of size {} but got {}", SZ, s.size()));
            memcpy(this, std::data(s), SZ);
        }

        byte_array &operator=(const buffer s)
        {
            if (s.size() != SZ) [[unlikely]]
                throw error(fmt::format("string_view must be of size {} but got {}", SZ, s.size()));
            memcpy(this, std::data(s), SZ);
            return *this;
        }

        byte_array &operator=(const std::string_view s)
        {
            if (s.size() != SZ) [[unlikely]]
                throw error(fmt::format("string_view must be of size {} but got {}", SZ, s.size()));
            memcpy(this, std::data(s), SZ);
            return *this;
        }

        [[nodiscard]] static consteval size_t num_bits()
        {
            return SZ * 8;
        }

        bool bit(const size_t bit_no) const
        {
            const auto byte_no = bit_no >> 3U;
            const auto byte_bit_no = size_t{7} - (bit_no & 0x7);
            if (byte_no >= SZ) [[unlikely]]
                throw error(fmt::format("a bit number {} is out of range for byte strings of {} bytes", bit_no, SZ));
            return base_type::operator[](byte_no) & (1U << byte_bit_no);
        }

        operator buffer() const noexcept
        {
            return { base_type::data(), SZ };
        }

        explicit operator std::string_view() const noexcept
        {
            return { reinterpret_cast<const char *>(base_type::data()), base_type::size() };
        }
    };

    extern void secure_clear(std::span<uint8_t> store);

    template<size_t SZ>
    struct secure_byte_array: byte_array<SZ>
    {
        using byte_array<SZ>::byte_array;

        static secure_byte_array<SZ> from_hex(const std::string_view hex)
        {
            return byte_array<SZ>::template from_hex<secure_byte_array<SZ>>(hex);
        }

        ~secure_byte_array()
        {
            secure_clear(*this);
        }
    };

    inline uint8_t uint_from_oct(char k)
    {
        switch (k) {
            case '0': return 0;
            case '1': return 1;
            case '2': return 2;
            case '3': return 3;
            case '4': return 4;
            case '5': return 5;
            case '6': return 6;
            case '7': return 7;
            [[unlikely]] default: throw error(fmt::format("unexpected character in an octal number: {}!", k));
        }
    }

    inline uint8_t uint_from_hex(uint8_t k)
    {
        static uint8_t map[256] {
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
        };
        if (const auto res = map[k]; res != 0xFF) [[likely]]
            return res;
        throw error(fmt::format("unexpected character in a hex number: {}!", k));
    }

    inline void init_from_hex_no_prefix(std::span<uint8_t> out, const std::string_view hex)
    {
        if (hex.size() != out.size() * 2) [[unlikely]]
            throw error(fmt::format("hex string must have {} characters but got {}: {}!", out.size() * 2, hex.size(), hex));
        for (size_t i = 0; i < out.size(); ++i)
            out[i] = static_cast<uint8_t>(uint_from_hex(hex[i * 2]) << 4U) | uint_from_hex(hex[i * 2 + 1]);
    }

    inline void init_from_hex(std::span<uint8_t> out, std::string_view hex)
    {
        if (hex.starts_with("0x"))
            hex = hex.substr(2);
        init_from_hex_no_prefix(out, hex);
    }

    struct uint8_vector: std::vector<uint8_t> {
        using base_type = std::vector<uint8_t>;
        using base_type::base_type;

        template<typename C=uint8_vector>
        static C from_hex(std::string_view hex)
        {
            if (hex.starts_with("0x"))
                hex = hex.substr(2);
            if (hex.size() % 2 != 0) [[unlikely]]
                throw error(fmt::format("hex string must have an even number of characters but got {}!", hex.size()));
            C data(hex.size() / 2);
            init_from_hex_no_prefix(data, hex);
            return data;
        }

        uint8_vector() noexcept =default;

        uint8_vector(base_type &&o) noexcept:
            base_type { std::move(o) }
        {
        }

        uint8_vector(const size_t sz):
            std::vector<uint8_t>(sz)
        {
        }

        uint8_vector(const buffer bytes):
            std::vector<uint8_t>(bytes.data(), bytes.data() + bytes.size())
        {
        }

        operator buffer() const noexcept
        {
            return { data(), size() };
        }

        std::string_view str() const noexcept
        {
            return { reinterpret_cast<const char *>(data()), size() };
        }

        uint8_vector &operator=(const buffer bytes)
        {
            resize(bytes.size());
            memcpy(data(), bytes.data(), bytes.size());
            return *this;
        }

        std::strong_ordering operator<=>(const buffer &o) const noexcept
        {
            return static_cast<buffer>(*this) <=> o;
        }

        std::strong_ordering operator<=>(const uint8_vector &o) const noexcept
        {
            return static_cast<buffer>(*this) <=> static_cast<buffer>(o);
        }

        bool operator==(const uint8_vector &o) const noexcept
        {
            return std::strong_ordering::equal == (*this <=> static_cast<buffer>(o));
        }

        bool operator==(const buffer &o) const noexcept
        {
            return std::strong_ordering::equal == (*this <=> o);
        }
    };

    static_assert(std::is_constructible_v<uint8_vector, buffer>);
    static_assert(std::is_constructible_v<buffer, uint8_vector>);
    static_assert(std::is_convertible_v<uint8_vector, buffer>);

    struct uninitialized_bytes_t {
        uninitialized_bytes_t(const uninitialized_bytes_t &) = delete;
        uninitialized_bytes_t &operator=(const uninitialized_bytes_t &) = delete;

        uninitialized_bytes_t() = default;

        explicit uninitialized_bytes_t(const size_t size):
            _size{size},
            _ptr{_size ? _alloc().allocate(size) : nullptr}
        {
        }

        uninitialized_bytes_t(uninitialized_bytes_t &&o) noexcept:
            _size{o._size},
            _ptr{o._ptr}
        {
            o._size = 0;
            o._ptr = nullptr;
        }

        uninitialized_bytes_t &operator=(uninitialized_bytes_t &&o) noexcept {
            if (this != &o) [[likely]] {
                if (_ptr)
                    _alloc().deallocate(_ptr, _size);
                _size = o._size;
                _ptr = o._ptr;
                o._size = 0;
                o._ptr = nullptr;
            }
            return *this;
        }

        ~uninitialized_bytes_t() {
            if (_ptr)
                _alloc().deallocate(_ptr, _size);
        }

        [[nodiscard]] size_t size() const noexcept {
            return _size;
        }

        [[nodiscard]] std::span<uint8_t> bytes() noexcept {
            return {_ptr, _size};
        }

        [[nodiscard]] uint8_t *data() noexcept {
            return _ptr;
        }

        [[nodiscard]] const uint8_t *data() const noexcept {
            return _ptr;
        }

        operator buffer() const noexcept {
            return {_ptr, _size};
        }
    private:
        size_t _size = 0;
        uint8_t *_ptr = nullptr;

        static std::allocator<uint8_t> &_alloc() noexcept {
            static std::allocator<uint8_t> alloc{};
            return alloc;
        }
    };

    static_assert(std::is_convertible_v<uninitialized_bytes_t, buffer>);

    struct buffer_lowercase: buffer {
        using buffer::buffer;
    };

    template<typename V>
    concept byte_append_container =
        std::same_as<typename V::value_type, uint8_t> &&
        requires(V v, const uint8_t b, const buffer buf) {
            { v.size() } -> std::convertible_to<size_t>;
            v.insert(v.end(), buf.begin(), buf.end());
            v.emplace_back(b);
        };

    template<byte_append_container V>
    V &append_bytes(V &v, const buffer &buf)
    {
        v.insert(v.end(), buf.begin(), buf.end());
        return v;
    }

    template<byte_append_container V>
    V &operator<<(V &v, const uint8_t b)
    {
        v.emplace_back(b);
        return v;
    }

    template<byte_append_container V>
    V &operator<<(V &v, const buffer &buf)
    {
        return append_bytes(v, buf);
    }
}

namespace fmt {
    template<size_t SZ>
    struct formatter<std::array<const uint8_t, SZ>>: formatter<std::span<const uint8_t>> {
    };

    template<size_t SZ>
    struct formatter<std::array<uint8_t, SZ>>: formatter<std::span<const uint8_t>> {
    };

    template<>
    struct formatter<turbo::buffer>: formatter<std::span<const uint8_t>> {
    };

    template<>
    struct formatter<turbo::uint8_vector>: formatter<turbo::buffer> {
    };

    template<>
    struct formatter<turbo::uninitialized_bytes_t>: formatter<turbo::buffer> {
    };

    template<>
    struct formatter<turbo::buffer_lowercase>: formatter<int> {
        template<typename FormatContext>
        auto format(const std::span<const uint8_t> &data, FormatContext &ctx) const -> decltype(ctx.out()) {
            auto out_it = ctx.out();
            for (uint8_t v: data) {
                out_it = fmt::format_to(out_it, "{:02x}", v);
            }
            return out_it;
        }
    };
}
