/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/file.hpp>
#include <turbo/common/test.hpp>
#include <turbo/plutus/flat-encoder.hpp>
#include <turbo/plutus/flat.hpp>

namespace {
    using namespace turbo;
    using namespace turbo::plutus;
    using namespace turbo::plutus::flat;
}

suite turbo_plutus_flat_suite = [] {
    using boost::ext::ut::v2_1_0::nothrow;
    using plutus::allocator;
    "turbo::plutus::flat"_test = [] {
        "term"_test = [] {
            for (auto &entry: std::filesystem::directory_iterator("./data/plutus/term")) {
                const auto script_path = entry.path().string();
                if (entry.is_regular_file() && entry.path().extension().string() == ".hex") {
                    const auto cbor = uint8_vector::from_hex(file::read(script_path).str());
                    const std::string exp_uplc { file::read(fmt::format("{}.uplc", (entry.path().parent_path() / entry.path().stem()).string())).str() };
                    allocator alloc {};
                    script s { alloc, cbor };
                    const auto act_uplc = fmt::format("{}", s);
                    expect_equal(exp_uplc, act_uplc, script_path);
                }
            }
        };
        "raw"_test = [] {
            auto bytes = uint8_vector::from_hex("0500023371C911071A5F783625EE8C004838B40181");
            expect(nothrow([&] {
                allocator alloc {};
                script s { alloc, bytes, false };
            }));
            // encoded program from the Plutus core spec
            for (const auto &raw_cbor: {
                    uint8_vector::from_hex("46010000222601"),
                    uint8_vector::from_hex("4D01000033222220051200120011"),
                    uint8_vector::from_hex("550100002225333573466644494400C0080045261601"),
                    uint8_vector::from_hex("58640100003222253335734646660020026EB0D5D09ABA2357446AE88D5D11ABA23574"
                                           "46AE88D5D118029ABA1300500223375E0026AE84DD60029112999AB9A35746004294054CCD5CD18009ABA100214A226660060066AE"
                                           "8800800452616235573C6EA80041"),
                    uint8_vector::from_hex("5883010000322233335734646660020026EB0D5D09ABA2357446AE88D5D11ABA235744"
                                           "6AE88D5D118021ABA1300400223375E00298011E581CFDB6C9683D3713A2C9DBCC835E6B547E71E1063DDC3E37C205909283002223"
                                           "33357346AE8C00892811999AB9A30023574200649448CCC014014D5D1002001A4C93124C4C9311AAB9E3754003")
            }) {
                expect(nothrow([&] {
                    allocator alloc {};
                    script s { alloc, raw_cbor };
                })) << fmt::format("{}", raw_cbor);
            }
        };
        "decoded values outlive non-owning input"_test = [] {
            for (const size_t payload_size: { size_t { 32 }, size_t { 300 } }) {
                allocator source_alloc {};
                uint8_vector payload(payload_size, uint8_t { 0xAB });
                const term source {
                    source_alloc,
                    plutus::constant {
                        source_alloc, bstr_type { source_alloc, buffer { payload } }
                    }
                };
                const auto encoded = encode(version { 1, 0, 0 }, source);

                allocator decode_alloc {};
                const auto decoded = [&] {
                    auto local_input = encoded;
                    return script { decode_alloc, buffer { local_input }, false };
                }();
                expect_equal(source, decoded.program());
            }
        };
        "literal builtin applications use a compact spine"_test = [] {
            allocator source_alloc {};
            const auto one = term { source_alloc,
                plutus::constant { source_alloc, bint_type { source_alloc, int64_t { 1 } } } };
            const auto two = term { source_alloc,
                plutus::constant { source_alloc, bint_type { source_alloc, int64_t { 2 } } } };
            auto expr = term { source_alloc, t_builtin { builtin_tag::add_integer } };
            expr = term { source_alloc, apply { std::move(expr), one } };
            expr = term { source_alloc, apply { std::move(expr), two } };
            const auto bytes = encode(version { 1, 0, 0 }, expr);

            allocator decode_alloc {};
            const script decoded { decode_alloc, buffer { bytes }, false };
            const auto decoded_expr = decoded.program();
            expect(decoded_expr.visit([](const auto &v) {
                return std::is_same_v<std::decay_t<decltype(v)>, t_builtin_spine>;
            }));
            expect_equal(expr, decoded_expr);
            expect_equal(bytes, encode(decoded.version(), decoded_expr));
            expect_equal(fmt::format("{}", expr), fmt::format("{}", decoded_expr));

            const auto unit = term { source_alloc,
                plutus::constant { source_alloc, std::monostate {} } };
            auto forced_expr = term { source_alloc, t_builtin { builtin_tag::choose_unit } };
            forced_expr = term { source_alloc, force { std::move(forced_expr) } };
            forced_expr = term { source_alloc, apply { std::move(forced_expr), unit } };
            forced_expr = term { source_alloc, apply { std::move(forced_expr), unit } };
            const auto forced_bytes = encode(version { 1, 0, 0 }, forced_expr);
            const script forced_decoded { decode_alloc, buffer { forced_bytes }, false };
            expect(forced_decoded.program().visit([](const auto &v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, t_builtin_spine>)
                    return v.b.tag == builtin_tag::choose_unit && v.forces == 1 && v.args.size() == 2;
                return false;
            }));
            expect_equal(forced_expr, forced_decoded.program());
            expect_equal(forced_bytes, encode(forced_decoded.version(), forced_decoded.program()));
        };
        "constr and case require UPLC 1.1"_test = [] {
            allocator source_alloc {};
            const term unit { source_alloc, plutus::constant { source_alloc, std::monostate {} } };
            const term constr {
                source_alloc,
                t_constr { 0, term_list { source_alloc, { unit } } }
            };
            const term acase {
                source_alloc,
                t_case { constr, term_list { source_alloc, { unit } } }
            };
            for (const auto &expr: { constr, acase }) {
                expect(throws([&] {
                    allocator decode_alloc {};
                    script s { decode_alloc, encode(version { 1, 0, 0 }, expr), false };
                }));
                expect(nothrow([&] {
                    allocator decode_alloc {};
                    script s { decode_alloc, encode(version { 1, 1, 0 }, expr), false };
                }));
            }
        };
        "ledger validation happens while decoding"_test = [] {
            allocator source_alloc {};
            const term expr {
                source_alloc,
                t_delay { term { source_alloc, t_builtin { builtin_tag::exp_mod_integer } } }
            };
            const auto bytes = encode(version { 1, 0, 0 }, expr);
            expect(nothrow([&] {
                allocator decode_alloc {};
                script s { decode_alloc, buffer { bytes }, false };
            }));
            expect(throws([&] {
                allocator decode_alloc {};
                script s { decode_alloc, buffer { bytes }, cardano::script_type::plutus_v3, 10, false };
            }));
            expect(nothrow([&] {
                allocator decode_alloc {};
                script s { decode_alloc, buffer { bytes }, cardano::script_type::plutus_v3, 11, false };
            }));

            asset_value::input_type entries {};
            asset_value::input_inner_type tokens {};
            tokens.emplace_back(asset_value::key_type { 0xBB }, cpp_int { 100 });
            entries.emplace_back(asset_value::key_type { 0xAA }, std::move(tokens));
            const term value_expr { source_alloc,
                plutus::constant { source_alloc, asset_value::from_list(source_alloc, std::move(entries)) } };
            const auto value_bytes = encode(version { 1, 0, 0 }, value_expr);
            expect(throws([&] {
                allocator decode_alloc {};
                script s { decode_alloc, buffer { value_bytes }, cardano::script_type::plutus_v3, 10, false };
            }));
            expect(nothrow([&] {
                allocator decode_alloc {};
                script s { decode_alloc, buffer { value_bytes }, cardano::script_type::plutus_v3, 11, false };
                expect_equal(value_expr, s.program());
            }));
        };
        "scripts"_test = [] {
            struct script_info {
                std::string path {};
                uint8_vector cbor {};
            };

            std::vector<script_info> scripts {};;
            for (auto &entry: std::filesystem::directory_iterator("./data/plutus/script-v2")) {
                const auto script_path = entry.path().string();
                if (entry.is_regular_file() && entry.path().extension().string() == ".bin")
                    scripts.emplace_back(script_path, file::read(script_path));
            }
            // sort by size so that errors can be debugged in the smaller scripts first
            std::sort(scripts.begin(), scripts.end(),
                      [](const auto &a, const auto &b) {
                          if (a.cbor.size() != b.cbor.size())
                              return a.cbor.size() < b.cbor.size();
                          return !a.cbor.empty() && memcmp(a.cbor.data(), b.cbor.data(), a.cbor.size()) < 0;
                      }
            );
            for (const auto &[path, cbor]: scripts) {
                expect(nothrow([&] {
                    allocator alloc {};
                    script s { alloc, cbor };
                }));
            }
        };
    };
};
