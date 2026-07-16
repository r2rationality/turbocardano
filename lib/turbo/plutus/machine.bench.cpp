/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/benchmark.hpp>
#include <turbo/config.hpp>
#include <turbo/plutus/machine.hpp>
#include <turbo/plutus/uplc.hpp>

using namespace turbo;
using namespace turbo::plutus;

suite plutus_machine_bench_suite = [] {
    "plutus::machine"_test = [] {
        "unique_ptr vs shared_ptr vs allocator"_test = [] {
            plutus::allocator alloc {};
            benchmark("std::shared_ptr", [&] {
                const auto p = std::make_shared<value::value_type>(plutus::constant { alloc, bint_type { alloc, 22 } });
                const volatile auto p2 = p;
            });
            benchmark("allocator", [&] {
                ankerl::nanobench::doNotOptimizeAway(alloc.make<value::value_type>(plutus::constant { alloc, bint_type { alloc, 22 } }));
            });
        };
        "switch variant.index() vs std::visit"_test = [] {
            using val_type = std::variant<uint64_t, std::string, uint8_vector>;
            std::vector<val_type> vals { { uint8_vector::from_hex("00112233") }, { "abc" }, { 123ULL } };
            benchmark("switch", [&] {
                uint64_t res = 0;
                for (const auto &val: vals) {
                    switch (val.index()) {
                        case 0: res += sizeof(uint64_t); break;
                        case 1: res += std::get<std::string>(val).size(); break;
                        case 2: res += std::get<uint8_vector>(val).size(); break;
                        default: throw error(fmt::format("unsupported variant index: {}", val.index()));
                    }
                }
                ankerl::nanobench::doNotOptimizeAway(res);
            }, vals.size());
            benchmark("visit", [&] {
                uint64_t res = 0;
                for (const auto &val: vals) {
                    res += std::visit([](const auto &v) {
                        using T = std::decay_t<decltype(v)>;
                        if constexpr (std::is_same_v<T, uint64_t>)
                            return sizeof(v);
                        else
                            return v.size();
                    }, val);
                }
                ankerl::nanobench::doNotOptimizeAway(res);
            });
        };
        {
            plutus::allocator s_alloc {};
            std::vector<uplc::script> scripts {};
            for (const auto &path: file::files_with_ext_path(install_path("./data/plutus/conformance/example"), ".uplc")) {
                if (!path.stem().string().starts_with("DivideByZero"))
                    scripts.emplace_back(s_alloc, file::read(path.string()));
            }
            auto eval = [&] {
                uint64_t total_steps = 0;
                for (const auto &s: scripts) {
                    plutus::allocator m_alloc {};
                    machine m { m_alloc };
                    const auto res = m.evaluate(s.program());
                    total_steps += res.cost.steps;
                }
                return total_steps;
            };
            benchmark(
                "conformance examples",
                eval,
                eval()
            );
        }
    };
};