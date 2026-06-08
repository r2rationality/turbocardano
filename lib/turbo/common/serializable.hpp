#pragma once
/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include <span>
#include <string>
#include <string_view>
#include <variant>
#include "error.hpp"

namespace turbo::codec {
    struct archive_t {
    };

    template<typename T>
        requires std::is_default_constructible_v<T>
    T from(auto &archive)
    {
        T res;
        archive.process(res);
        return res;
    }

    template<typename T>
    using variant_names_t = std::array<std::string_view, std::variant_size_v<T>>;

    struct variant_index_overrides_t {
        using decoder_override_map_t = std::map<uint8_t, size_t>;
        using encoder_override_map_t = std::map<size_t, uint8_t>;
        using init_type = decoder_override_map_t::value_type;

        decoder_override_map_t decode_overrides;
        encoder_override_map_t encode_overrides{};

        explicit variant_index_overrides_t(const std::initializer_list<init_type> o):
            decode_overrides{o}
        {
            for (const auto &[ci, vi]: decode_overrides) {
                encode_overrides.try_emplace(vi, ci);
            }
        }
    };

    template<typename T, size_t I>
    void variant_set_type(T &val, const size_t requested_type, auto &archive)
    {
        if (requested_type >= std::variant_size_v<T>) [[unlikely]]
            throw error(fmt::format("an unsupported type value {} for {}", requested_type, typeid(T).name()));
        if constexpr (I < std::variant_size_v<T>) {
            if (requested_type > I)
                return variant_set_type<T, I + 1>(val, requested_type, archive);
            if (requested_type < I) [[unlikely]]
                throw error(fmt::format("internal error: an incomplete traversal of type {}", typeid(T).name()));
            val = codec::from<std::variant_alternative_t<I, T>>(archive);
        }
    }

    template<typename T>
    concept has_emplace_c = requires(T t, typename T::value_type v)
    {
        t.emplace_hint_unique(t.end(), std::move(v));
    };

    template<typename T>
    concept varlen_uint_c = requires(T t) {
        { t.value() } -> std::integral;
        t = typename T::base_type{};
    };

    template<typename T>
    concept optional_like_c = requires(T t) {
        t.reset();
        t.emplace();
        { t.has_value() } -> std::same_as<bool>;
        *t;
    };

    template<typename T>
    concept serializable_c = requires(T t, archive_t a)
    {
        { t.serialize(a) } -> std::same_as<void>;
    };

    template<typename T>
    concept single_line_serializable_c = serializable_c<T>
        && requires {
            { T::single_line_serialization } -> std::convertible_to<bool>;
        }
        && T::single_line_serialization;

    // Structural byte-buffer concepts. The !serializable_c guard prevents false matches on
    // sequence_t<uint8_t,N> or fixed_sequence_t<uint8_t,N>, which are structurally identical
    // but have a serialize() that encodes elements individually rather than as a raw blob.

    // Fixed-size byte buffer: indexable uint8_t elements, no dynamic resizing.
    template<typename T>
    concept byte_array_like_c = !serializable_c<T>
        && requires(T& t, size_t i) {
            { t.data() } -> std::same_as<uint8_t*>;
            { t[i] }     -> std::same_as<uint8_t&>;
        } && !requires(T& t) { t.resize(size_t{}); };

    // Dynamic byte sequence: uint8_t data pointer with runtime resize.
    template<typename T>
    concept byte_sequence_like_c = !serializable_c<T>
        && requires(T& t) {
            { t.data() } -> std::same_as<uint8_t*>;
            t.resize(size_t{});
        };

    template<typename T>
    concept has_foreach_c = requires(T t, typename T::observer_t obs)
    {
        { t.foreach(obs) };
    };

    template<typename T>
    concept not_serializable_c = !serializable_c<T>;

    // Fixed-element array: type is tagged with is_element_sequence to distinguish from raw byte arrays.
    template<typename T>
    concept fixed_array_like_c = !serializable_c<T>
        && requires { T::is_element_sequence; };

    // Bounded dynamic range: has static min_size/max_size and is a range (e.g. sequence_t, set_t).
    template<typename T>
    concept bounded_range_c = !serializable_c<T>
        && std::ranges::range<T>
        && requires {
            { T::min_size } -> std::convertible_to<size_t>;
            { T::max_size } -> std::convertible_to<size_t>;
        };

    // Map-like: has key_type/mapped_type and a static config() with key_name/val_name.
    template<typename T>
    concept map_like_c = !serializable_c<T>
        && requires {
            typename T::key_type;
            typename T::mapped_type;
            { T::config().key_name } -> std::convertible_to<std::string_view>;
            { T::config().val_name } -> std::convertible_to<std::string_view>;
        };

    template<bounded_range_c T>
    void check_bounds(const size_t sz)
    {
        if (!(static_cast<int>(sz >= T::min_size) & static_cast<int>(sz <= T::max_size))) [[unlikely]]
            throw error(fmt::format("array size {} is out of allowed bounds: [{}, {}]", sz, T::min_size, T::max_size));
    }

    template<bounded_range_c T>
    void check_bounds(const T &val)
    {
        check_bounds<T>(val.size());
    }

    template<typename T>
    struct as_variant_t {
        T &val;
        const variant_names_t<T> &names;
        const variant_index_overrides_t *overrides = nullptr;
    };

    template<typename T>
    as_variant_t<T> as_variant(T &val, const variant_names_t<T> &names,
                               const variant_index_overrides_t *ov = nullptr)
    {
        return { val, names, ov };
    }

    template<typename T>
    concept archive_formattable_c = serializable_c<T>
        || varlen_uint_c<T>
        || optional_like_c<T>
        || fixed_array_like_c<T>
        || bounded_range_c<T>
        || map_like_c<T>
        || byte_array_like_c<T>
        || byte_sequence_like_c<T>;

    template<typename OUT_IT>
    struct formatter: archive_t {
        static constexpr size_t shift = 4;

        explicit formatter(OUT_IT it):
            _it { std::move(it) }
        {
        }

        void push(const std::string_view)
        {
            ++_depth;
        }

        void pop()
        {
            --_depth;
        }

        template<typename T>
        void format(const T &val)
        {
            if constexpr (serializable_c<T>) {
                // serialize() is intentionally non-const across the codebase (it's used for both
                // encoding and decoding); const_cast is safe here as format() only reads via the archive
                const_cast<T &>(val).serialize(*this);
            }  else if constexpr (varlen_uint_c<T>) {
                _it = fmt::format_to(_it, "{}", val.value());
                _ends_with_newline = false;
            } else if constexpr (optional_like_c<T>) {
                if (val) {
                    format(*val);
                } else {
                    _it = fmt::format_to(_it, "std::nullopt");
                    _ends_with_newline = false;
                }
            } else if constexpr (fixed_array_like_c<T> || bounded_range_c<T>) {
                if constexpr (bounded_range_c<T>)
                    check_bounds(val);
                _it = fmt::format_to(_it, "[\n");
                ++_depth;
                for (const auto &v: val) {
                    if constexpr (_multiline_value<std::remove_cvref_t<decltype(v)>>) {
                        format(v);
                    } else {
                        _it = fmt::format_to(_it, "{:{}}", "", _depth * shift);
                        format(v);
                        _it = fmt::format_to(_it, "\n");
                    }
                }
                --_depth;
                _it = fmt::format_to(_it, "{:{}}](size: {})", "", _depth * shift, val.size());
                _ends_with_newline = false;
            } else if constexpr (map_like_c<T>) {
                _it = fmt::format_to(_it, "{{\n");
                ++_depth;
                if constexpr (has_foreach_c<T>) {
                    val.foreach([&](const auto &k, const auto &v) {
                        _it = fmt::format_to(_it, "{:{}}", "", _depth * shift);
                        format(k);
                        _it = fmt::format_to(_it, ": ");
                        format(v);
                        _it = fmt::format_to(_it, "\n");
                    });
                } else {
                    for (const auto &[k, v]: val) {
                        _it = fmt::format_to(_it, "{:{}}", "", _depth * shift);
                        format(k);
                        _it = fmt::format_to(_it, ": ");
                        format(v);
                        _it = fmt::format_to(_it, "\n");
                    }
                }
                --_depth;
                _it = fmt::format_to(_it, "{:{}}}}(size: {})", "", _depth * shift, val.size());
                _ends_with_newline = false;
            } else if constexpr (byte_array_like_c<T> || byte_sequence_like_c<T>) {
                _it = fmt::format_to(_it, "{}", std::span<const uint8_t>{val.data(), val.size()});
                _ends_with_newline = false;
            } else if constexpr (std::is_same_v<T, uint8_t>
                    || std::is_same_v<T, uint16_t>
                    || std::is_same_v<T, uint32_t>
                    || std::is_same_v<T, uint64_t>
                    || std::is_same_v<T, int64_t>
                    || std::is_same_v<T, bool>
                    || std::is_same_v<T, std::string_view>
                    || std::is_same_v<T, std::string>
                    || std::is_convertible_v<T, std::span<const uint8_t>>) {
                _it = fmt::format_to(_it, "{}", val);
                _ends_with_newline = false;
            } else {
                throw error(fmt::format("formatter serialization is not enabled for type {}", typeid(T).name()));
            }
        }

        void process(const auto &val)
        {
            format(val);
        }

        void process(const std::string_view name, const auto &val)
        {
            _it = fmt::format_to(_it, "{:{}}", "", _depth * shift);
            _it = fmt::format_to(_it, "{}:", name);
            using value_type = std::remove_cvref_t<decltype(val)>;
            if constexpr (_collection_value<value_type>) {
                _it = fmt::format_to(_it, " ");
                format(val);
                if (_depth > 0) {
                    _it = fmt::format_to(_it, "\n");
                    _ends_with_newline = true;
                }
            } else {
                ++_depth;
                if constexpr (_multiline_value<value_type>) {
                    _it = fmt::format_to(_it, "\n");
                } else {
                    _it = fmt::format_to(_it, " ");
                }
                format(val);
                --_depth;
                if (!_ends_with_newline)
                    _it = fmt::format_to(_it, "\n");
                _ends_with_newline = true;
            }
        }

        template<typename T>
        void process(as_variant_t<T> av)
        {
            auto ci = av.val.index();
            _it = fmt::format_to(_it, "<{}>: ", av.names[ci]);
            std::visit([&](const auto &vv) {
                process(vv);
            }, av.val);
        }

        OUT_IT it() const
        {
            return _it;
        }
    private:
        template<typename T>
        static constexpr bool _multiline_value = (serializable_c<T> && !single_line_serializable_c<T>)
            || fixed_array_like_c<T>
            || bounded_range_c<T>
            || map_like_c<T>;

        template<typename T>
        static constexpr bool _collection_value = fixed_array_like_c<T>
            || bounded_range_c<T>
            || map_like_c<T>;

        OUT_IT _it;
        size_t _depth = 0;
        bool _ends_with_newline = false;
    };
}

namespace fmt {
    template<turbo::codec::archive_formattable_c T>
    struct formatter<T>: formatter<std::string_view> {
        template<typename FormatContext>
        auto format(const T &v, FormatContext &ctx) const -> decltype(ctx.out())
        {
            turbo::codec::formatter<decltype(ctx.out())> frmtr { ctx.out() };
            frmtr.format(v);
            return frmtr.it();
        }
    };
}
