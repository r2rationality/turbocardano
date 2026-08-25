/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <array>
#include <boost/container/adaptive_pool.hpp>
#include <boost/pool/pool_alloc.hpp>
#include <map>
#include <turbo/common/benchmark.hpp>
#include <turbo/partitioned-map.hpp>

namespace {
    using namespace turbo;

    struct bench_key {
        std::array<uint8_t, 16> bytes {};

        bool operator==(const bench_key &) const =default;
        auto operator<=>(const bench_key &) const =default;
    };

    struct bench_value {
        std::array<uint64_t, 12> words {};
    };

    bench_key make_key(const uint64_t value)
    {
        bench_key key {};
        key.bytes[0] = static_cast<uint8_t>(value);
        for (size_t i = 1; i < 9; ++i)
            key.bytes[i] = static_cast<uint8_t>(value >> ((i - 1) * 8));
        return key;
    }

    template<typename Allocator>
    using map_partition = std::map<bench_key, bench_value, std::less<bench_key>, Allocator>;

    using value_type = std::pair<const bench_key, bench_value>;
    using standard_map = partitioned_map<
        bench_key,
        bench_value,
        map_partition<std::allocator<value_type>>>;
    using turbo_pool_map = partitioned_map<bench_key, bench_value>;
    using boost_fast_pool_map = partitioned_map<
        bench_key,
        bench_value,
        map_partition<boost::fast_pool_allocator<value_type>>>;
    using boost_adaptive_map = partitioned_map<
        bench_key,
        bench_value,
        map_partition<boost::container::adaptive_pool<value_type, 0x1000, 2, 5, 1>>>;
    using boost_private_adaptive_map = partitioned_map<
        bench_key,
        bench_value,
        map_partition<boost::container::private_adaptive_pool<value_type, 0x1000, 2, 5, 1>>>;

    template<typename Map>
    void allocator_workload()
    {
        static constexpr size_t num_items = 0x40000;
        Map values {};
        for (size_t i = 0; i < num_items; ++i)
            values.try_emplace(make_key(i));
        for (size_t i = 0; i < num_items; i += 2)
            values.erase(make_key(i));
        for (size_t i = 0; i < num_items; i += 2)
            values.try_emplace(make_key(i));
        ankerl::nanobench::doNotOptimizeAway(values.size());
        values.clear();
    }
}

suite partitioned_map_bench_suite = [] {
    "partitioned_map::allocator"_test = [] {
        static constexpr size_t num_operations = 0x40000 * 3;
        ankerl::nanobench::Bench b {};
        b.title("partitioned_map allocator: insert/erase/reinsert/clear")
            .output(&std::cerr)
            .unit("operation")
            .performanceCounters(true)
            .epochs(3)
            .minEpochIterations(1)
            .batch(num_operations)
            .relative(true);
        b.run("std::allocator (mimalloc override)", [] { allocator_workload<standard_map>(); });
        b.run("turbo::pool_allocator", [] { allocator_workload<turbo_pool_map>(); });
        b.run("boost::fast_pool_allocator (singleton)", [] { allocator_workload<boost_fast_pool_map>(); });
        b.run("boost::container::adaptive_pool (shared)", [] { allocator_workload<boost_adaptive_map>(); });
        b.run("boost::container::private_adaptive_pool", [] { allocator_workload<boost_private_adaptive_map>(); });
    };
};
