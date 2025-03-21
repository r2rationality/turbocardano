/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */
#include <dt/common/benchmark.hpp>
#include <dt/scheduler.hpp>
#include <dt/vrf.hpp>

using namespace boost::ut;
using namespace daedalus_turbo;

suite vrf_bench_suite = [] {
    "vrf"_test = [] {
        const auto vkey = uint8_vector::from_hex("5ca0ed2b774adf3bae5e3e7da2f8ec877d9f063cc3d7050a6c49dfbbe2641dec");
        const auto proof = uint8_vector::from_hex("02180c447320b66012420971b70b448d11fead6d6e334c398f4daf01ccd92bfbcc4a8730a296ab33241f72da3c3a1fd53f1206a2b9f27ff6a5d9b8860fd955c39f55f9293ab58d1a2c18d555d2686101");
        const auto result =uint8_vector::from_hex("deb23fdc1267fa447fb087796544ce02b0580df8f1927450bed0df134ddc3548075ed48ffd72ae2a9ea65f79429cfbe2e15b625cb239ad0ec3910003765a8eb3");
        const auto msg = uint8_vector::from_hex("fc9f719740f900ee2809f6fdcf31bb6f096f0af133c604a27aaf85379c");
        benchmark_r("vrf/verify", 2000.0, 2000, [&] {
            vrf03_verify(result, vkey, proof, msg);
        });
        size_t num_iters = 1000;
        scheduler sched {};
        benchmark_r("vrf/verify parallel", 10000.0, 3, [&] {
            for (size_t i = 0; i < sched.num_workers(); ++i) {
                sched.submit("signature_ok", 100, [&]() {
                    for (size_t i = 0; i < num_iters; ++i)
                        vrf03_verify(result, vkey, proof, msg);
                    return num_iters;
                });
            }
            sched.process(false);
            return sched.num_workers() * num_iters;
        });
    };
};