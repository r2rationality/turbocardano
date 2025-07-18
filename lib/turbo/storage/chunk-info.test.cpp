/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/common/test.hpp>
#include <turbo/storage/chunk-info.hpp>

using namespace turbo;

suite storage_chunk_info_suite = [] {
    "storage::chunk_info"_test = [] {
        "construct default"_test = [] {
            storage::chunk_info chunk {};
            expect(chunk.offset == 0_ull);
            expect(chunk.data_size == 0_ull);
            expect(chunk.data_hash == cardano::block_hash::from_hex("0000000000000000000000000000000000000000000000000000000000000000"));
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