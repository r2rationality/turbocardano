/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/auxiliary-data.hpp>
#include <turbo/common/test.hpp>

using namespace turbo;
using namespace turbo::cardano;

suite cardano_dijkstra_auxiliary_data_suite = [] {
    "cardano::dijkstra::auxiliary-data"_test = [] {
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
            const auto auxiliary_data = dijkstra::auxiliary_data_t::from_cbor(parsed.get());
            const auto &metadata = std::get<dijkstra::metadata_t>(auxiliary_data.value);
            const auto &datum = metadata.dict.at(0);
            const auto &items = std::get<shelley::metadatum_t::map_t>(datum.value);

            expect(fatal(expect_equal(3, items.size())));
            expect_equal(2, std::get<shelley::nint64_t>(items.at(0).first.value));
            expect_equal(20, std::get<shelley::nint64_t>(items.at(0).second.value));
            expect_equal(1, std::get<shelley::nint64_t>(items.at(1).first.value));
            expect_equal(10, std::get<shelley::nint64_t>(items.at(1).second.value));
            expect_equal(2, std::get<shelley::nint64_t>(items.at(2).first.value));
            expect_equal(21, std::get<shelley::nint64_t>(items.at(2).second.value));

            era_encoder enc { era_t::dijkstra };
            auxiliary_data.to_cbor(enc);
            expect_equal(raw, enc.cbor());
        };
    };
};
