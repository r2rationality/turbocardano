/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/test.hpp>
#include <turbo/storage/chunk-info.hpp>
#include <turbo/zpp.hpp>

using namespace turbo;

suite storage_chunk_info_suite = [] {
    "storage::chunk_info"_test = [] {
        "construct default"_test = [] {
            storage::chunk_info chunk {};
            expect(chunk.offset == 0_ull);
            expect(chunk.data_size == 0_ull);
            expect(chunk.compression_level == 0);
            expect(chunk.data_hash == cardano::block_hash::from_hex("0000000000000000000000000000000000000000000000000000000000000000"));
        };
        "binary metadata preserves chunk boundaries and blocks"_test = [] {
            storage::chunk_info chunk {
                .offset=100,
                .data_size=22,
                .first_slot=42,
                .last_block={ 43, cardano::block_hash::from_hex("1111111111111111111111111111111111111111111111111111111111111111") },
                .num_blocks=1
            };
            chunk.blocks.push_back({ .hash=chunk.last_block.hash, .offset=100, .size=22, .slot=43, .era=5 });
            const auto restored = turbo::zpp::deserialize<storage::chunk_info>(turbo::zpp::serialize(chunk));
            expect_equal(restored.offset, chunk.offset);
            expect(restored.to_json() == chunk.to_json());
            expect(fatal(restored.blocks.size() == 1));
            expect(restored.blocks.front() == chunk.blocks.front());
            expect_equal(restored.blocks.front().size, 22);
            expect_equal(restored.blocks.front().era, 5);
        };

        "JSON block counts accept the size_t range and reject negative values"_test = [] {
            auto metadata = storage::chunk_info {}.to_json();
            constexpr auto maximum = std::numeric_limits<size_t>::max();
            metadata["numBlocks"] = maximum;
            expect_equal(storage::chunk_info::from_json(metadata).num_blocks, maximum);
            metadata["numBlocks"] = -1;
            expect(throws([&] { storage::chunk_info::from_json(metadata); }));
        };

        "rel_path"_test = [] {
            storage::chunk_info chunk {};
            expect(chunk.rel_path() == "chunk/0000000000000000000000000000000000000000000000000000000000000000.zstd");
            chunk.data_hash = cardano::block_hash::from_hex("1111111111111111111111111111111111111111111111111111111111111111");
            expect(chunk.rel_path() == "chunk/1111111111111111111111111111111111111111111111111111111111111111.zstd");
        };
        "end_offset"_test = [] {
            storage::chunk_info chunk {};
            expect(chunk.end_offset() == 0_ull);
            chunk.data_size = 22;
            expect(chunk.end_offset() == 22_ull);
            chunk.offset = 78;
            expect(chunk.end_offset() == 100_ull);
        };
    };
};
