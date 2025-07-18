/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cbor/compare.hpp>
#include <turbo/cbor/zero2.hpp>

namespace turbo::cbor {
    using namespace zero2;

    void diff_list::add(diff_t &&d)
    {
        emplace_back(std::move(d));
        if (size() >= 100) [[unlikely]]
            throw error(fmt::format("terminating the comparison early - too many differences: {}", *this));
    }

    static diff_list compare_uint(const uint64_t v1, const uint64_t v2, path_t &path)
    {
        diff_list diffs {};
        if (v1 != v2) [[unlikely]]
            diffs.add(path, "expected: {} actual: {}", v1, v2);
        return diffs;
    }

    static diff_list compare_nint(const uint64_t v1, const uint64_t v2, path_t &path)
    {
        diff_list diffs {};
        if (v1 != v2) [[unlikely]]
            diffs.add(path, "expected: -({}) actual: -({})", v1, v2);
        return diffs;
    }

    static diff_list compare_text(value &v1, value &v2, path_t &path)
    {
        diff_list diffs {};
        if (v1.indefinite() != v2.indefinite()) [[unlikely]] {
            diffs.add(path, "expected: {} text actual {} text",
                                                    v1.indefinite() ? "chunked" : "normal", v2.indefinite() ? "chunked" : "normal");
        } else {
            if (!v1.indefinite()) [[likely]] {
                if (v1.text() != v2.text()) [[unlikely]]
                    diffs.add(path, "expected: {} actual: {}", v1.to_string(), v2.to_string());
            } else {
                std::string t1 {}, t2 {};
                v1.to_text(t1);
                v2.to_text(t2);
                if (t1 != t2) [[unlikely]]
                    diffs.add(path, "expected: T '{}' actual: T '{}'", t1, t2);
            }
        }
        return diffs;
    }

    static diff_list compare_bytes(value &v1, value &v2, path_t &path)
    {
        diff_list diffs {};
        if (v1.indefinite() != v2.indefinite()) [[unlikely]] {
            diffs.add(path, "expected: {} bytes actual {} bytes",
                                                    v1.indefinite() ? "chunked" : "normal", v2.indefinite() ? "chunked" : "normal");
        } else {
            if (!v1.indefinite()) [[likely]] {
                if (v1.bytes() != v2.bytes()) [[unlikely]]
                    diffs.add(path, "expected: {} actual: {}", v1.to_string(), v2.to_string());
            } else {
                uint8_vector b1 {}, b2 {};
                v1.to_bytes(b1);
                v2.to_bytes(b2);
                if (b1 != b2) [[unlikely]]
                    diffs.add(path, "expected: B #{} actual: B #{}'", b1, b2);
            }
        }
        return diffs;
    }

    static diff_list compare(value &v1, value &v2, path_t &path);

    template<typename T>
    struct value_stream {
        const char *name;
        T &it;
        bool empty = false;

        T *operator->()
        {
            return &it;
        }

        value &read()
        {
            return it.read();
        }

        bool done()
        {
            if (!empty) {
                empty = it.done();
            }
            return empty;
        }
    };

    static diff_list compare_array(value &v1, value &v2, path_t &path)
    {

        value_stream s1 { "missing", v1.array() };
        value_stream s2 { "extra", v2.array() };
        diff_list diffs {};
        size_t i = 0;
        while (!s1.done() && !s2.done()) {
            path.push(i);
            if (auto diff = compare(s1->read(), s2->read(), path); !diff.empty()) [[unlikely]] {
                for (auto &&d: diff)
                    diffs.add(std::move(d));
            }
            path.pop();
            ++i;
        }
        for (auto *s: std::initializer_list<value_stream<array_reader> *> { &s1, &s2 }) {
            size_t cnt = 0;
            while (!s->done()) {
                s->read();
                ++cnt;
            }
            if (cnt) [[unlikely]]
                diffs.add(path, "{} items: {} after #{}", s->name, cnt, i);
        }
        return diffs;
    }

    static diff_list compare_map(value &v1, value &v2, path_t &path)
    {
        diff_list diffs {};
        value_stream s1 { "missing", v1.map() };
        value_stream s2 { "extra", v2.map() };
        size_t i = 0;
        while (!s1.done() && !s2.done()) {
            auto &k1 = s1->read_key();
            auto &k2 = s2->read_key();
            path.push(i);
            if (auto diff = compare(k1, k2, path); !diff.empty()) [[unlikely]] {
                for (auto &&d: diff)
                    diffs.add(std::move(d));
            }
            auto &v1 = s1->read_val(std::move(k1));
            auto &v2 = s2->read_val(std::move(k2));
            if (auto diff = compare(v1, v2, path); !diff.empty()) [[unlikely]] {
                for (auto &&d: diff)
                    diffs.add(std::move(d));
            }
            path.pop();
            ++i;
        }
        struct it_info {
            std::string_view name;
            map_reader &it;
        };
        for (auto *s: std::initializer_list<value_stream<map_reader> *> { &s1, &s2 }) {
            size_t cnt = 0;
            while (!s->done()) {
                auto &k = (*s)->read_key();
                (*s)->read_val(std::move(k));
                ++cnt;
            }
            if (cnt) [[unlikely]]
                diffs.add(path, "{} items: {} after #{}", s->name, cnt, i);
        }
        return diffs;
    }

    static diff_list compare_tag(value &v1, value &v2, path_t &path)
    {
        diff_list diffs {};
        auto &t1 = v1.tag();
        auto &t2 = v2.tag();
        if (t1.id() != t2.id()) [[unlikely]]
            diffs.add(path, "expected id: {} actual id: {}", t1.id(), t2.id());
        auto &c1 = t1.read();
        auto &c2 = t2.read();
        if (auto diff = compare(c1, c2, path); !diff.empty()) [[unlikely]] {
            for (auto &&d: diff)
                diffs.add(std::move(d));
        }
        return diffs;
    }

    static diff_list compare_simple(value &v1, value &v2, path_t &path)
    {
        if (v1.special() != v2.special()) [[unlikely]]
            return diff_list { diff_t { path, "expected simple: {} actual simple: {}",
                                                    static_cast<int>(v1.special()), static_cast<int>(v2.special()) } };
        return {};
    }

    static diff_list compare(value &v1, value &v2, path_t &path)
    {
        if (v1.type() != v2.type()) [[unlikely]] {
            return diff_list { diff_t { path, "types do not match: expected: {} actual: {}", v1.type(), v2.type() } };
        }
        switch (const auto typ = v1.type(); typ) {
            case major_type::uint: return compare_uint(v1.uint(), v2.uint(), path);
            case major_type::nint: return compare_nint(v1.nint(), v2.nint(), path);
            case major_type::text: return compare_text(v1, v2, path);
            case major_type::bytes: return compare_bytes(v1, v2, path);
            case major_type::array: return compare_array(v1, v2, path);
            case major_type::map: return compare_map(v1, v2, path);
            case major_type::tag: return compare_tag(v1, v2, path);
            case major_type::simple: return compare_simple(v1, v2, path);
            [[unlikely]] default: throw error(fmt::format("can't compare cbor type: {}", typ));
        }
    }

    diff_list compare(const buffer &expected, const buffer &actual)
    {
        diff_list diffs {};
        path_t path {};
        decoder it1 { expected };
        decoder it2 { actual };
        size_t i = 0;
        while (!it1.done() && !it2.done()) {
            path.push(i);
            if (auto diff = compare(it1.read(), it2.read(), path); !diff.empty()) [[unlikely]] {
                for (auto &&d: diff)
                    diffs.add(std::move(d));
            }
            path.pop();
            ++i;
        }
        struct it_info {
            std::string_view name;
            decoder &it;
        };
        for (const auto &it: std::initializer_list<it_info>{ { "missing", it1 }, { "extra", it2 } }) {
            size_t cnt = 0;
            while (!it.it.done()) {
                it.it.read();
                ++cnt;
            }
            if (cnt) [[unlikely]]
                diffs.add(path, "{} items: {} after #{}", it.name, cnt, i);
        }
        return diffs;
    }
}