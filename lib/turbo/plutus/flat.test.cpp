/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/common/file.hpp>
#include <turbo/common/test.hpp>
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
                          return memcmp(a.cbor.data(), b.cbor.data(), a.cbor.size()) < 0;
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