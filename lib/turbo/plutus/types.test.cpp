/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

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
        "builtin_tags"_test = [] {
            expect_equal(58, static_cast<int>(builtin_tag::bls12_381_g1_compress));
            expect_equal(59, static_cast<int>(builtin_tag::bls12_381_g1_uncompress));
            expect_equal(60, static_cast<int>(builtin_tag::bls12_381_g1_hash_to_group));
            expect_equal(65, static_cast<int>(builtin_tag::bls12_381_g2_compress));
            expect_equal(66, static_cast<int>(builtin_tag::bls12_381_g2_uncompress));
            expect_equal(67, static_cast<int>(builtin_tag::bls12_381_g2_hash_to_group));
        };
        "builtin arguments use immutable fixed-capacity storage"_test = [] {
            allocator alloc {};
            const auto unit = value::unit(alloc);
            const auto boolean = value::boolean(alloc, true);
            const builtin_args empty {};
            const builtin_args one { alloc, empty, unit };
            const builtin_args two { alloc, one, boolean };
            const builtin_args three { alloc, two, unit };
            const builtin_args four { alloc, three, unit };
            const builtin_args five { alloc, four, unit };
            const builtin_args six { alloc, five, unit };

            expect_equal(0, empty.size());
            expect_equal(1, one.size());
            expect_equal(2, two.size());
            expect(one.at(0) == unit);
            expect(two.at(0) == unit);
            expect(two.at(1) == boolean);
            expect_equal(builtin_args::max_size, six.size());
            expect(throws([&] { builtin_args { alloc, six, unit }; }));
        };
    };
};
