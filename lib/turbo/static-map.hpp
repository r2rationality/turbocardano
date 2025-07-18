#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <algorithm>
#include <turbo/partitioned-map.hpp>
#include <unordered_map>
#include <vector>

namespace turbo {
    template<typename K, typename V>
    struct static_map {
        constexpr static auto serialize(auto &archive, auto &self)
        {
            return archive(self._data);
        }

        using value_type = std::pair<K, V>;
        using key_type = K;
        using mapped_type = V;
        using storage_type = std::vector<value_type>;
        using const_iterator = storage_type::const_iterator;
        using iterator = storage_type::iterator;

        static_map()
        {
        }

        static_map(static_map<K, V> &&src)
            : _data { std::move(src._data) }
        {
        }

        static_map(const static_map<K, V> &src)
            : _data { src._data }
        {
        }

        bool operator==(const static_map &o) const
        {
            return _data == o._data;
        }

        static_map<K, V> &operator=(static_map<K, V> &&src)
        {
            _data = std::move(src._data);
            return *this;
        }

        static_map<K, V> &operator=(const std::map<K, V> &src)
        {
            _data.clear();
            _data.reserve(src.size());
            // relies on the fact that std::map keeps elements in a sorted order, so there no need to re-sort the data for binary search
            for (const auto &[k, v]: src)
                _data.emplace_back(k, v);
            return *this;
        }

        static_map<K, V> &operator=(const partitioned_map<K, V> &src)
        {
            _data.clear();
            _data.reserve(src.size());
            // relies on the fact that partitioned_map keeps elements in a sorted order, so there no need to re-sort the data for binary search
            for (size_t part_idx = 0; part_idx < src.num_parts; ++part_idx) {
                for (const auto &[k, v]: src.partition(part_idx))
                    _data.emplace_back(k, v);
            }
            return *this;
        }

        static_map<K, V> &operator=(const std::unordered_map<K, V> &src)
        {
            _data.clear();
            _data.reserve(src.size());
            for (const auto &[k, v]: src)
                _data.emplace_back(k, v);
            // must sort since unordered_map does not guarantee the order
            sort();
            return *this;
        }

        static_map<K, V> &operator=(const static_map<K, V> &src)
        {
            _data = src._data;
            return *this;
        }

        void clear()
        {
            _data.clear();
        }

        bool empty() const
        {
            return _data.empty();
        }

        size_t size() const
        {
            return _data.size();
        }

        void reserve(const size_t exp_size)
        {
            _data.reserve(exp_size);
        }

        void resize(const size_t new_size)
        {
            _data.resize(new_size);
        }

        void emplace_back(const K &k, const V &v)
        {
            _data.emplace_back(k, v);
        }

        void schrink_to_fit()
        {
            _data.shrink_to_fit();
        }

        void sort()
        {
            std::sort(_data.begin(), _data.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        }

        bool contains(const K &k) const
        {
            return find(k) != end();
        }

        value_type &at_idx(size_t idx)
        {
            return _data.at(idx);
        }

        const V &at(const K &k) const
        {
            if (auto it = find(k); it != end())
                return it->second;
            throw error(fmt::format("unknown key: {}", k));
        }

        const V get(const K &k) const
        {
            V res {};
            auto it = find(k);
            if (it != end())
                res = it->second;
            return res;
        }

        iterator begin()
        {
            return _data.begin();
        }

        iterator end()
        {
            return _data.end();
        }

        const_iterator begin() const
        {
            return _data.begin();
        }

        const_iterator end() const
        {
            return _data.end();
        }

        const_iterator find(const K &k) const
        {
            auto it = std::lower_bound(_data.begin(), _data.end(), k, [&](const auto &el, const auto &val) { return el.first < val; });
            if (it != _data.end() && it->first == k)
                return it;
            return end();
        }

        const storage_type &storage() const
        {
            return _data;
        }
    protected:
        storage_type _data {};
    };
}

namespace fmt {
    template<typename K, typename V>
    struct formatter<turbo::static_map<K, V>>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{{ {} }}", v.storage());
        }
    };
}