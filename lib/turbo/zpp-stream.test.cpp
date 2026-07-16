/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/test.hpp>
#include <turbo/zpp-stream.hpp>

namespace {
    using namespace turbo;
}

suite zpp_suite = [] {
    "zpp_stream"_test = [] {
        std::vector<int> exp { 1, 2, 3 };
        file::tmp tmp { "test-zpp-1.bin" };
        {
            zpp_stream::write_stream ws { tmp };
            for (const auto &i: exp)
                ws.write(i);
        }
        std::vector<int> act {};
        {
            zpp_stream::read_stream rs { tmp };
            while (!rs.eof())
                act.emplace_back(rs.read<int>());
        }
        expect_equal(exp, act, tmp.path());
    };
};
