/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include <turbo/common/test.hpp>
#include <vector>
#include "memory.hpp"
#include "timer.hpp"

using namespace turbo;

namespace {
    void touch_pages(uint8_vector &data)
    {
        static constexpr size_t page_size = 4096;
        for (size_t off = 0; off < data.size(); off += page_size) {
            volatile auto *p = data.data() + off;
            *p = static_cast<uint8_t>(off);
        }
        if (!data.empty()) {
            volatile auto *p = data.data() + data.size() - 1;
            *p = static_cast<uint8_t>(data.size());
        }
    }
}

suite turbo_common_memory_suite = [] {
    "turbo::common::memory"_test = [] {
        "my_usage_mb"_test = [] {
            const timer t{"my_usage_mb"};
            const auto before = memory::my_usage_mb();
            static constexpr size_t chunk_size = 64ULL << 20U; // 256 MB
            static constexpr size_t max_chunks = 64;
            std::vector<uint8_vector> chunks{};
            auto after = memory::my_usage_mb();
            for (size_t i = 0; i < max_chunks && after <= before; ++i) {
                auto &data = chunks.emplace_back(chunk_size, static_cast<uint8_t>(i));
                // Force memory writes so that the memory is committed and visible in process usage.
                for (auto volatile *p = data.data(), *end = data.data() + data.size(); p < end; p += 0x1000U) {
                    *p ^= 0xFFU;
                }
                after = memory::my_usage_mb();
            }
            expect(after > before) << after << before;
            // Some standard libraries do not immediately return the memory to the OS, thus, not checking for the memory release
        };
    };
};
