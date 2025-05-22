/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/common/coro.hpp>
#include <dt/common/test.hpp>

using namespace daedalus_turbo;

namespace {
    coro::generator_task_t<int> my_coro()
    {
        co_yield 22;
        co_yield 33;
    }
}

suite coroutine_suite = [] {
    "coro"_test = [] {
        std::vector<int> v {};
        auto gen = my_coro();
        while (gen.resume()) {
            v.emplace_back(gen.take());
        }
        test_same(std::vector<int> { 22, 33 }, v);
    };
};