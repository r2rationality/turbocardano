/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/benchmark.hpp>
#include "big-int.hpp"
#include "rational.hpp"

using namespace boost::ut;
using namespace turbo;

namespace {
    uint64_t max_pool_fp64(uint64_t reward_pot, uint64_t total_stake, uint64_t pledge, uint64_t pool_stake, uint64_t n_opt, double a0)
    {
        double z0 = 1.0 / n_opt;
        double pool_s = std::min(static_cast<double>(pool_stake) / total_stake, z0);
        double pledge_s = std::min(static_cast<double>(pledge) / total_stake, z0);
        double y = (z0 - pool_s) / z0;
        double x = (pool_s - pledge_s * y) / z0;
        return static_cast<uint64_t>(static_cast<double>(reward_pot) / (1.0 + a0) * (pool_s + pledge_s * a0 * x));
    }

    uint64_t max_pool_rat(uint64_t reward_pot, uint64_t total_stake, uint64_t pledge, uint64_t pool_stake, uint64_t n_opt, double a0)
    {
        cpp_rational z0 = 1;
        z0 /= n_opt;
        cpp_rational pool_s = pool_stake;
        pool_s /= total_stake;
        pool_s = std::min(pool_s, z0);
        cpp_rational pledge_s = pledge;
        pledge_s /= total_stake;
        pledge_s = std::min(pledge_s, z0);
        auto y = (z0 - pool_s) / z0;
        auto x = (pool_s - pledge_s * y) / z0;
        return static_cast<uint64_t>(reward_pot / (1.0 + a0) * (pool_s + pledge_s * a0 * x));
    }

    uint64_t member_reward_f64(uint64_t max_reward, uint64_t pool_stake, uint64_t deleg_stake, uint64_t cost, const cpp_rational &margin)
    {
        return static_cast<uint64_t>((max_reward - cost) * (1 - static_cast<double>(margin)) * deleg_stake / pool_stake);
    }

    uint64_t member_reward_rat(uint64_t max_reward, uint64_t pool_stake, uint64_t deleg_stake, uint64_t cost, const cpp_rational & margin)
    {
        cpp_rational reward = max_reward - cost;
        reward *= (1 - margin);
        reward *= deleg_stake;
        reward /= pool_stake;
        return static_cast<uint64_t>(reward);
    }
}

suite rational_bench_suite = [] {
    "rational"_test = [] {
        {
            uint64_t r = 31834688329017;
            uint64_t total_stake = 31737719158318701;
            uint64_t pledge = 10000000000;
            uint64_t pool_stake = 94511860029536;
            uint64_t n_opt = 150;
            double a0 = 0.3;
            expect(max_pool_fp64(r, total_stake, pledge, pool_stake, n_opt, a0) == max_pool_rat(r, total_stake, pledge, pool_stake, n_opt, a0));
            benchmark("max_pool_f64", [&] {
                max_pool_fp64(r, total_stake, pledge, pool_stake, n_opt, a0);
            });
            benchmark("max_pool_rat", [&] {
                max_pool_rat(r, total_stake, pledge, pool_stake, n_opt, a0);
            });
        }
        {
            uint64_t cost = 340000000;
            uint64_t deleg_stake = 8642660310954;
            uint64_t pool_stake = 61139181786687;
            uint64_t pool_reward_pot = 43128231125;
            cpp_rational margin { 1, 100 };
            benchmark("member_reward_fp64", [&] {
                member_reward_f64(pool_reward_pot, pool_stake, deleg_stake, cost, margin);
            });
            benchmark("member_reward_rat", [&] {
                member_reward_rat(pool_reward_pot, pool_stake, deleg_stake, cost, margin);
            });
        }
    };
};