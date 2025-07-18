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
    T from(auto &archive)
    {
        T res;
        res.serialize(archive);
        return res;
    }

    template<typename T>
    using variant_names_t = std::array<std::string_view, std::variant_size_v<T>>;

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

    template<typename T, size_t I>
    void variant_get_type(T &val, const size_t requested_type, auto &archive)
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
    concept has_emplace_c = requires(T t)
    {
        { t.emplace() };
    };

    template<typename T>
    concept serializable_c = requires(T t, archive_t a)
    {
        { t.serialize(a) } -> std::same_as<void>;
    };

    template<typename T>
    concept has_foreach_c = requires(T t, typename T::observer_t obs)
    {
        { t.foreach(obs) };
    };

    template<typename T>
    concept not_serializable_c = !serializable_c<T>;

    template<typename OUT_IT>
    struct formatter: archive_t {
        static constexpr size_t shift = 2;

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
                ++_depth;
                const_cast<T &>(val).serialize(*this);
                --_depth;
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
            } else {
                throw error(fmt::format("formatter serialization is not enabled for type {}", typeid(T).name()));
            }
        }

        void process_varlen_uint(const auto &val)
        {
            format(val);
        }

        void process_uint(const auto &val)
        {
            format(val);
        }

        void process(const auto &val)
        {
            //_it = fmt::format_to(_it, "{:{}}", "", _depth * shift);
            format(val);
        }

        void process(const std::string_view name, const auto &val)
        {
            _it = fmt::format_to(_it, "{:{}}", "", _depth * shift);
            _it = fmt::format_to(_it, "{}: ", name);
            ++_depth;
            format(val);
            --_depth;
            _it = fmt::format_to(_it, "\n");
        }

        void process_map_item(const auto &k, const auto &v)
        {
            format(k);
            _it = fmt::format_to(_it, ": ");
            format(v);
        }

        void process_map(const auto &m, const std::string_view, const std::string_view)
        {
            _it = fmt::format_to(_it, "{:{}}{{", "", _depth * shift);
            if (!m.empty()) {
                ++_depth;
                _it = fmt::format_to(_it, "\n");
                using T = std::decay_t<decltype(m)>;
                if constexpr (has_foreach_c<T>) {
                    m.foreach([&](const auto &k, const auto &v) {
                        _it = fmt::format_to(_it, "{:{}}", "", _depth * shift);
                        process_map_item(k, v);
                        _it = fmt::format_to(_it, "\n");
                    });
                } else if constexpr (std::ranges::range<T>) {
                    for (const auto &[k, v]: m) {
                        _it = fmt::format_to(_it, "{:{}}", "", _depth * shift);
                        process_map_item(k, v);
                        _it = fmt::format_to(_it, "\n");
                    }
                } else {
                    throw error(fmt::format("process_map does not support type: {}", typeid(decltype(m)).name()));
                }
                --_depth;
                _it = fmt::format_to(_it, "{:{}}", "", _depth * shift);
            }
            _it = fmt::format_to(_it, "}}(size: {})\n", m.size());
        }

        void process_array(const auto &arr, const size_t min_sz=0, const size_t max_sz=std::numeric_limits<size_t>::max())
        {
            (void)min_sz;
            (void)max_sz;
            _it = fmt::format_to(_it, "{:{}}[", "", _depth * shift);
            if (!arr.empty()) {
                ++_depth;
                _it = fmt::format_to(_it, "\n");
                for (const auto &v: arr) {
                    _it = fmt::format_to(_it, "{:{}}", "", _depth * shift);
                    format(v);
                    _it = fmt::format_to(_it, "\n");
                }
                --_depth;
                _it = fmt::format_to(_it, "{:{}}", "", _depth * shift);
            }
            _it = fmt::format_to(_it, "](size: {})\n", arr.size());
        }

        void process_array_fixed(const auto &self)
        {
            process_array(self);
        }

        template<typename T>
        void process_optional(const T &val)
        {
            if (val) {
                process(*val);
            } else {
                _it = fmt::format_to(_it, "{:{}}std::nullopt", "", _depth * shift);
            }
        }

        template<typename T>
        void process_variant(const T &val, const codec::variant_names_t<T> &names)
        {
            std::visit([&](const auto &vv) {
                process(names.at(val.index()), vv);
            }, val);
        }

        void process_bytes(const std::span<const uint8_t> bytes)
        {
            _it = fmt::format_to(_it, "{:{}}#{}", "", _depth * shift, bytes);
        }

        void process_bytes_fixed(const std::span<const uint8_t> bytes)
        {
            _it = fmt::format_to(_it, "{:{}}#{}", "", _depth * shift, bytes);
        }

        OUT_IT it() const
        {
            return _it;
        }
    private:
        OUT_IT _it;
        size_t _depth = 0;
    };
}

namespace fmt {
    template<turbo::codec::serializable_c T>
    struct formatter<T>: formatter<int> {
        template<typename FormatContext>
        auto format(const T &v, FormatContext &ctx) const -> decltype(ctx.out())
        {
            turbo::codec::formatter<decltype(ctx.out())> frmtr { ctx.out() };
            frmtr.format(v);
            return frmtr.it();
        }
    };
}
