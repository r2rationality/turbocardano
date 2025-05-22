#pragma once
#ifndef DAEDALUS_TURBO_CONTAINER_HPP
#define DAEDALUS_TURBO_CONTAINER_HPP
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <boost/container/flat_set.hpp>
#include <boost/container/flat_map.hpp>
#include <dt/common/format.hpp>

namespace daedalus_turbo {
    template<typename T>
    using vector = std::vector<T>;

    template<typename K, typename V>
    using map = std::map<K, V>;

    template<typename K, typename V>
    using multimap = std::multimap<K, V>;

    template<typename T, typename C=std::less<T>>
    using set = std::set<T, C>;

    template<typename K, typename V>
    using unordered_map = std::unordered_map<K, V>;

    template<typename T>
    using unordered_set = std::unordered_set<T>;

    template<typename K>
    struct flat_set: boost::container::flat_set<K> {
        using base_type = boost::container::flat_set<K>;
        using base_type::base_type;

        const typename base_type::value_type &at(const size_t idx) const
        {
            if (idx >= base_type::size()) [[unlikely]]
                throw error(fmt::format("flat_map index out of range: {} >= {}", idx, base_type::size()));
            auto it = base_type::cbegin() + idx;
            return *it;
        }
    };

    template<typename K, typename V>
    struct flat_map: boost::container::flat_map<K, V> {
        using base_type = boost::container::flat_map<K, V>;
        using base_type::base_type;

        const typename base_type::value_type &nth_or_die(const size_t idx) const
        {
            if (const auto it = base_type::nth(idx); it != base_type::end()) [[likely]]
                return *it;
            throw error(fmt::format("attempted to reference a missing index: {}", idx));
        }
    };
}

namespace fmt {
    template<typename T>
    struct formatter<daedalus_turbo::flat_set<T>>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            auto out_it = fmt::format_to(ctx.out(), "[");
            for (auto it = v.begin(); it != v.end(); ++it) {
                const std::string sep { std::next(it) == v.end() ? "" : ", " };
                out_it = fmt::format_to(out_it, "{}{}", *it, sep);
            }
            return fmt::format_to(out_it, "]");
        }
    };

    template<typename K, typename V>
    struct formatter<daedalus_turbo::flat_map<K, V>>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            auto out_it = fmt::format_to(ctx.out(), "{{");
            for (auto it = v.begin(); it != v.end(); ++it) {
                const std::string sep { std::next(it) == v.end() ? "" : ", " };
                out_it = fmt::format_to(out_it, "{}={}{}", it->first, it->second, sep);
            }
            return fmt::format_to(out_it, "}}");
        }
    };
}

#endif //!DAEDALUS_TURBO_CONTAINER_HPP