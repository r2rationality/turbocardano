/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/benchmark.hpp>
#include <turbo/zpp-stream.hpp>

using namespace boost::ut;
using namespace turbo;

suite zpp_stream_bench_suite = [] {
    "zpp_stream"_test = [] {
        static constexpr size_t num_items = 1e5;
        using data_type = std::array<size_t, 10>;
        benchmark("write and read cycle", [] {
            file::tmp tmp { "bench-zpp-1.bin" };
            {
                zpp_stream::write_stream ws { tmp };
                data_type data {};
                for (size_t i = 0; i < num_items; ++i) {
                    std::ranges::fill(data, i);
                    ws.write(data);
                }
            }
            {
                zpp_stream::read_stream rs { tmp };
                for (size_t i = 0; i < num_items; ++i) {
                    const auto data = rs.read<data_type>();
                    for (const auto act_i: data)
                        if (act_i != i) [[unlikely]]
                            throw error(fmt::format("unexpected value: {} when waiting for {}", act_i, i));
                }
            }
        }, num_items * sizeof(data_type) * 2);
    };
};
