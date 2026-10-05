/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/shelley/metadata.hpp>
#include <turbo/common/test.hpp>

using namespace turbo;
using namespace turbo::cardano;

suite cardano_shelley_metadata_suite = [] {
    "cardano::shelley::metadata"_test = [] {
        "nested metadata maps preserve order and duplicate keys"_test = [] {
            const auto raw = uint8_vector::from_hex(
                "A1" // metadata with one label
                "00" // label 0
                "A3" // metadatum map with three entries
                "0214" // 2 => 20
                "010A" // 1 => 10
                "0215" // 2 => 21
            );
            auto parsed = cbor::zero2::parse(raw);
            const auto metadata = shelley::metadata_t::from_cbor(parsed.get());
            const auto &datum = metadata.dict.at(0);
            const auto &items = std::get<shelley::metadatum_t::map_t>(datum.value);

            expect(fatal(expect_equal(3, items.size())));
            expect_equal(2, std::get<uint64_t>(items.at(0).first.value));
            expect_equal(20, std::get<uint64_t>(items.at(0).second.value));
            expect_equal(1, std::get<uint64_t>(items.at(1).first.value));
            expect_equal(10, std::get<uint64_t>(items.at(1).second.value));
            expect_equal(2, std::get<uint64_t>(items.at(2).first.value));
            expect_equal(21, std::get<uint64_t>(items.at(2).second.value));

            era_encoder enc { era_t::shelley };
            metadata.to_cbor(enc);
            expect_equal(raw, enc.cbor());
        };

        "metadata integers preserve the full CBOR range"_test = [] {
            const auto raw = uint8_vector::from_hex(
                "A10087" // metadata label 0 => seven integers
                "20" // -1
                "3B7FFFFFFFFFFFFFFF" // -2^63
                "3B8000000000000000" // -2^63 - 1
                "3BFFFFFFFFFFFFFFFF" // -2^64
                "00" // 0
                "1B8000000000000000" // 2^63
                "1BFFFFFFFFFFFFFFFF" // 2^64 - 1
            );
            auto parsed = cbor::zero2::parse(raw);
            const auto metadata = shelley::metadata_t::from_cbor(parsed.get());
            const auto &items = std::get<shelley::metadatum_t::array_t>(metadata.dict.at(0).value);

            expect(fatal(expect_equal(7, items.size())));
            expect_equal(uint64_t { 0 }, std::get<shelley::nint64_t>(items.at(0).value).raw);
            expect_equal(uint64_t { 0x7FFFFFFFFFFFFFFF }, std::get<shelley::nint64_t>(items.at(1).value).raw);
            expect_equal(uint64_t { 0x8000000000000000 }, std::get<shelley::nint64_t>(items.at(2).value).raw);
            expect_equal(uint64_t { 0xFFFFFFFFFFFFFFFF }, std::get<shelley::nint64_t>(items.at(3).value).raw);
            expect_equal(uint64_t { 0 }, std::get<uint64_t>(items.at(4).value));
            expect_equal(uint64_t { 0x8000000000000000 }, std::get<uint64_t>(items.at(5).value));
            expect_equal(uint64_t { 0xFFFFFFFFFFFFFFFF }, std::get<uint64_t>(items.at(6).value));

            expect(items.at(3) < items.at(2));
            expect(items.at(2) < items.at(1));
            expect(items.at(1) < items.at(0));
            expect(items.at(0) < items.at(4));
            expect(items.at(4) < items.at(5));
            expect(items.at(5) < items.at(6));

            era_encoder enc { era_t::shelley };
            metadata.to_cbor(enc);
            expect_equal(raw, enc.cbor());
        };
    };
};
