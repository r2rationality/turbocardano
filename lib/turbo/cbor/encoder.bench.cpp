/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <nanobench.h>
#include <turbo/container/flat_map.hpp>
#include <turbo/container/flat_set.hpp>
#include <turbo/cbor/encoder.hpp>
#include <turbo/cbor/zero2.hpp>
#include <turbo/common/benchmark.hpp>
#include <turbo/zpp.hpp>

namespace {
    using namespace boost::ut;
    using namespace turbo;
    namespace dt = turbo;

    using test_item_set = std::set<uint64_t>;
    using test_item = std::map<std::string, test_item_set>;

    uint8_vector encode(const test_item& items)
    {
        cbor::encoder enc {};
        enc.map(items.size());
        for (const auto &[k, vals]: items) {
            enc.text(k);
            enc.array(vals.size());
            for (const auto v: vals)
                enc.uint(v);
        }
        return std::move(enc.cbor());
    }

    template<typename T>
    constexpr bool has_reserve()
    {
        return std::is_member_function_pointer_v<decltype(&T::reserve)>;
    }

    template<typename M>
    M decode_reserve(const buffer data)
    {
        auto pv = cbor::zero2::parse(data);
        auto &v = pv.get();
        M out_items {};
        auto &m_it = v.map();
        if (!v.indefinite())
            out_items.reserve(v.special_uint());
        while (!m_it.done()) {
            auto &mk = m_it.read_key();
            std::string k_str { mk.text() };
            auto &mv = m_it.read_val(std::move(mk));
            typename M::mapped_type vals {};
            if (!mv.indefinite())
                vals.reserve(mv.special_uint());
            auto &v_it = mv.array();
            while (!v_it.done()) {
                vals.emplace(v_it.read().uint());
            }
            out_items.try_emplace(std::move(k_str), std::move(vals));
        }
        return out_items;
    }

    template<typename M>
    M decode_reserve_hint(const buffer data)
    {
        auto pv = cbor::zero2::parse(data);
        auto &v = pv.get();
        M out_items {};
        auto &m_it = v.map();
        if (!v.indefinite())
            out_items.reserve(v.special_uint());
        while (!m_it.done()) {
            auto &mk = m_it.read_key();
            std::string k_str { mk.text() };
            auto &mv = m_it.read_val(std::move(mk));
            typename M::mapped_type vals {};
            if (!mv.indefinite())
                vals.reserve(mv.special_uint());
            auto &v_it = mv.array();
            while (!v_it.done()) {
                vals.emplace_hint(vals.end(), v_it.read().uint());
            }
            out_items.try_emplace(out_items.end(), std::move(k_str), std::move(vals));
        }
        return out_items;
    }

    template<typename M>
    M decode(const buffer data)
    {
        auto pv = cbor::zero2::parse(data);
        auto &v = pv.get();
        M out_items {};
        auto &m_it = v.map();
        while (!m_it.done()) {
            auto &mk = m_it.read_key();
            std::string k_str { mk.text() };
            auto &mv = m_it.read_val(std::move(mk));
            typename M::mapped_type vals {};
            auto &v_it = mv.array();
            while (!v_it.done()) {
                vals.emplace(v_it.read().uint());
            }
            out_items.try_emplace(std::move(k_str), std::move(vals));
        }
        return out_items;
    }
}

suite cbor_encoder_bench_suite = [] {
    "cbor::encoder"_test = [] {
        "map of sets"_test = [] {
            test_item items {};
            static constexpr size_t num_items = 1 << 20;
            static constexpr size_t data_size = sizeof(test_item)
                + num_items * sizeof(test_item::node_type) + (sizeof(test_item_set) + sizeof(test_item_set::node_type) * 1.5);
            for (size_t i = 0; i < num_items; ++i) {
                auto &set = items[fmt::format("item{}", i)];
                if (i % 2) {
                    set.emplace(1234);
                    set.emplace(12);
                } else {
                    set.emplace(0);
                }
            }
            ankerl::nanobench::Bench b {};
            b.title("Serialization/Deserialization")
                .output(&std::cerr)
                .unit("byte")
                .performanceCounters(true)
                .relative(true)
                .batch(data_size);
            {
                const auto data = encode(items);
                b.run("cbor: serialize",[&] {
                    ankerl::nanobench::doNotOptimizeAway(encode(items));
                });
                b.run("cbor: deserialize std::map+std::set", [&] {
                    ankerl::nanobench::doNotOptimizeAway(decode<test_item>(data));
                });
                b.run("cbor: deserialize flat_map+boost::flat_set", [&] {
                    using set_type = flat_set<uint64_t>;
                    using map_type = flat_map<std::string, set_type>;
                    ankerl::nanobench::doNotOptimizeAway(decode<map_type>(data));
                });
                b.run("cbor: deserialize boost::flat_map+boost::flat_set + reserve", [&] {
                    using set_type = flat_set<uint64_t>;
                    using map_type = flat_map<std::string, set_type>;
                    ankerl::nanobench::doNotOptimizeAway(decode_reserve<map_type>(data));
                });
                b.run("cbor: deserialize boost::flat_map+boost::flat_set + reserve + hint", [&] {
                    using set_type = flat_set<uint64_t>;
                    using map_type = flat_map<std::string, set_type>;
                    ankerl::nanobench::doNotOptimizeAway(decode_reserve_hint<map_type>(data));
                });
            }
            {
                b.run("zpp: serialize",[&] {
                    ankerl::nanobench::doNotOptimizeAway(dt::zpp::serialize(items));
                });
                const auto data = dt::zpp::serialize(items);
                b.run("zpp: deserialize std::map+std::set", [&] {
                    ankerl::nanobench::doNotOptimizeAway(dt::zpp::deserialize<test_item>(data));
                });
            }
        };
    };
};
