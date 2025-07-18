/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include <turbo/common/test.hpp>
#include "memory.hpp"

using namespace turbo;

suite turbo_common_memory_suite = [] {
    "turbo::common::memory"_test = [] {
        const auto before = memory::my_usage_mb();
        static constexpr size_t alloc_size = 0x4000000;
        size_t after_alloc;
        {
            uint8_vector data(alloc_size);
            // force memory writes so that the memory is really allocated
            for (auto volatile *p = data.data(), *end = data.data() + data.size(); p < end; ++p)
                *p = p - data.data();
            after_alloc = memory::my_usage_mb();
        }
        expect(after_alloc >= before + (alloc_size >> 20)) << after_alloc << before;
        // Some standard libraries do not immediately return the memory to the OS, thus, not checking for the memory release
    };
};