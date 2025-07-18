/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/common/test.hpp>
#include <turbo/plutus/types.hpp>

using namespace turbo;
using namespace turbo::plutus;

suite plutus_types_suite = [] {
    using namespace std::string_literals;
    "plutus::types"_test = [] {
        "version"_test = [&] {
            {
                const version v { "0.1.2" };
                expect_equal(0, v.major);
                expect_equal(1, v.minor);
                expect_equal(2, v.patch);
                expect_equal("0.1.2"s, static_cast<std::string>(v));
                expect(throws([]{ version { "0.2" }; }));
                expect(throws([]{ version { ".0.2.3" }; }));
                expect(throws([]{ version { "0.b.3" }; }));
                expect(throws([]{ version { "0.1.." }; }));
                expect(throws([]{ version { "0.1.3." }; }));
                expect(throws([]{ version { "0.1.3c" }; }));
                expect(throws([]{ version { " 0.1.3" }; }));
                expect(throws([]{ version { "0.1.3 " }; }));
            }
            {
                expect_equal(true, version { "1.1.0" } >= "1.0.0");
                expect_equal(true, version { "1.1.0" } >= "1.1.0");
                expect_equal(true, version { "2.0.0" } >= "1.9.10");
                expect_equal(true, version { "1.2.0" } >= "1.1.10");
                expect_equal(true, version { "1.2.1" } >= "1.2.0");
                expect_equal(false, version { "1.1.0" } >= "1.1.1");
                expect_equal(false, version { "1.1.9" } >= "1.2.3");
                expect_equal(false, version { "1.2.3" } >= "2.0.7");
            }
        };
    };
};