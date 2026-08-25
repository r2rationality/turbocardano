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
        "empty byte-string buffer operations"_test = [] {
            allocator alloc {};
            const buffer empty {};
            const bstr_type value { alloc, empty };
            expect(value->empty());

            bstr_type::value_type bytes { alloc, empty };
            bytes = empty;
            bytes << empty;
            expect(bytes.empty());
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
        "terms use a pointer-sized tagged handle"_test = [] {
            allocator alloc {};
            const term unit { alloc, plutus::constant { alloc, std::monostate {} } };
            const term variable_val { alloc, variable { 3 } };
            const term delay_val { alloc, t_delay { unit } };
            const term force_val { alloc, force { unit } };
            const term lambda_val { alloc, t_lambda { unit } };
            const term apply_val { alloc, plutus::apply { unit, unit } };
            const term failure_val { alloc, failure {} };
            const term builtin_val { alloc, t_builtin { builtin_tag::add_integer } };
            const term constr_val { alloc, t_constr { 0, term_list { alloc, term_list::value_type { alloc } } } };
            const term case_val { alloc, t_case { unit, term_list { alloc, term_list::value_type { alloc } } } };

            const auto tag = [](const term &v) {
                return v.visit([](const auto &payload) {
                    using T = std::decay_t<decltype(payload)>;
                    if constexpr (std::is_same_v<T, variable>)
                        return 0;
                    else if constexpr (std::is_same_v<T, t_delay>)
                        return 1;
                    else if constexpr (std::is_same_v<T, force>)
                        return 2;
                    else if constexpr (std::is_same_v<T, t_lambda>)
                        return 3;
                    else if constexpr (std::is_same_v<T, plutus::apply>)
                        return 4;
                    else if constexpr (std::is_same_v<T, t_builtin_spine>)
                        return 4;
                    else if constexpr (std::is_same_v<T, plutus::constant>)
                        return 5;
                    else if constexpr (std::is_same_v<T, failure>)
                        return 6;
                    else if constexpr (std::is_same_v<T, t_builtin>)
                        return 7;
                    else if constexpr (std::is_same_v<T, t_constr>)
                        return 8;
                    else if constexpr (std::is_same_v<T, t_case>)
                        return 9;
                });
            };

            expect_equal(sizeof(void *), sizeof(term));
            expect_equal(0, tag(variable_val));
            expect_equal(1, tag(delay_val));
            expect_equal(2, tag(force_val));
            expect_equal(3, tag(lambda_val));
            expect_equal(4, tag(apply_val));
            expect_equal(5, tag(unit));
            expect_equal(6, tag(failure_val));
            expect_equal(7, tag(builtin_val));
            expect_equal(8, tag(constr_val));
            expect_equal(9, tag(case_val));
        };
        "runtime values use a pointer-sized tagged handle"_test = [] {
            allocator alloc {};
            const term expr { alloc, plutus::constant { alloc, std::monostate {} } };
            const environment env {};
            const value constant_val = value::unit(alloc);
            const value delay_val { alloc, v_delay { env, expr } };
            const value lambda_val { alloc, v_lambda { env, expr } };
            const value builtin_val { alloc, v_builtin { t_builtin { builtin_tag::add_integer }, {} } };
            const value constr_val { alloc, v_constr { 0, value_list { alloc } } };

            const auto tag = [](const value &v) {
                return v.visit([](const auto &payload) {
                    using T = std::decay_t<decltype(payload)>;
                    if constexpr (std::is_same_v<T, plutus::constant>)
                        return 0;
                    else if constexpr (std::is_same_v<T, v_delay>)
                        return 1;
                    else if constexpr (std::is_same_v<T, v_lambda>)
                        return 2;
                    else if constexpr (std::is_same_v<T, v_builtin>)
                        return 3;
                    else if constexpr (std::is_same_v<T, v_constr>)
                        return 4;
                });
            };

            expect_equal(sizeof(void *), sizeof(value));
            expect_equal(0, tag(constant_val));
            expect_equal(1, tag(delay_val));
            expect_equal(2, tag(lambda_val));
            expect_equal(3, tag(builtin_val));
            expect_equal(4, tag(constr_val));
            constant_val.as_unit();
            expect(throws([&] { delay_val.as_const(); }));
        };
    };
};
