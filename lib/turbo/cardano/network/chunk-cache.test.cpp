/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <future>
#include <turbo/common/test.hpp>
#include <turbo/common/zstd.hpp>
#include "chunk-cache.hpp"

namespace {
    using namespace turbo;
    using namespace turbo::cardano;
    using namespace turbo::cardano::network;

    block_hash key(uint8_t id)
    {
        block_hash hash {};
        hash[0] = id;
        return hash;
    }
}

suite cardano_network_chunk_cache_suite = [] {
    "cardano::network::chunk_cache"_test = [] {
        "shared buffers and admission before loading"_test = [] {
            chunk_cache cache { 16 };
            size_t loads = 0;
            const auto load = [&](write_buffer) { ++loads; };
            auto a = cache.get(key(1), 16, load);
            auto b = cache.get(key(1), 16, load);
            expect(a == b);
            expect_equal(loads, 1);
            expect(throws([&] { cache.get(key(1), 8, load); }));
            expect(throws([&] { cache.get(key(2), 17, load); }));
            expect(throws([&] { cache.get(key(2), 1, load); }));
            expect_equal(loads, 1);
            a.reset();
            b.reset();
            expect(nothrow([&] { cache.get(key(2), 16, load); }));
            expect_equal(loads, 2);
        };

        "eviction follows recency and skips active readers"_test = [] {
            chunk_cache cache { 24 };
            const auto load = [](write_buffer) {};
            auto pinned = cache.get(key(1), 8, load);
            std::weak_ptr<const uint8_vector> second = cache.get(key(2), 8, load);
            std::weak_ptr<const uint8_vector> third = cache.get(key(3), 8, load);
            cache.get(key(2), 8, load);
            cache.get(key(4), 8, load);
            expect(!second.expired());
            expect(third.expired());
            expect(pinned == cache.get(key(1), 8, load));
        };

        "concurrent requests share one reservation and load"_test = [] {
            chunk_cache cache { 16 };
            std::promise<void> started, release, follower_started;
            auto released = release.get_future().share();
            std::future<chunk_cache::data_ptr> first, second;
            const auto load = [&](write_buffer out) {
                started.set_value();
                released.wait();
                out[0] = 7;
            };
            scope_exit unblock { [&] {
                release.set_value();
                if (first.valid()) first.wait();
                if (second.valid()) second.wait();
            } };
            first = std::async(std::launch::async, [&] { return cache.get(key(1), 16, load); });
            started.get_future().wait();
            second = std::async(std::launch::async, [&] {
                follower_started.set_value();
                return cache.get(key(1), 16, [](write_buffer) { throw error("duplicate chunk load"); });
            });
            follower_started.get_future().wait();
            expect(second.wait_for(std::chrono::milliseconds { 20 }) == std::future_status::timeout);
            size_t rejected_loads = 0;
            expect(throws([&] { cache.get(key(2), 1, [&](write_buffer) { ++rejected_loads; }); }));
            expect_equal(rejected_loads, 0);
            release.set_value();
            unblock.release();
            const auto a = first.get();
            const auto b = second.get();
            expect(a == b);
            expect_equal(a->at(0), 7);
        };

        "oversized frames fail within the reservation and allow retries"_test = [] {
            chunk_cache cache { 8 };
            const auto compressed = zstd::compress(uint8_vector(16), 1);
            expect(throws([&] {
                cache.get(key(1), 8, [&](write_buffer bytes) { zstd::decompress(bytes, compressed); });
            }));
            const auto load = [](write_buffer) {};
            expect(nothrow([&] { cache.get(key(2), 8, load); }));
            expect(nothrow([&] { cache.get(key(1), 8, load); }));
        };
    };
};
