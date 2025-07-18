/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/common/test.hpp>
#include <turbo/index/merge-zpp.hpp>

using namespace turbo;

suite index_merge_suite = [] {
    "index::merge"_test = [] {
        file::tmp t1 { "test-index-merge-1" };
        file::tmp t2 { "test-index-merge-2" };
        file::tmp t3 { "test-index-merge-3" };
        {
            zpp_stream::write_stream ws { t1 };
            ws.write<int>(22);
            ws.write<int>(44);
        }
        {
            zpp_stream::write_stream ws { t2 };
            ws.write<int>(00);
            ws.write<int>(66);
        }
        {
            zpp_stream::write_stream ws { t3 };
            ws.write<int>(11);
            ws.write<int>(99);
        }
        file::tmp to { "test-index-merge-out" };
        index::merge_zpp<int>(to, std::vector<std::string> { t1, t2, t3 });
        std::vector<int> exp { 00, 11, 22, 44, 66, 99 };
        std::vector<int> act {};
        zpp_stream::read_stream rs { to };
        while (!rs.eof())
            act.emplace_back(rs.read<int>());
        expect_equal(exp, act);
    };
};