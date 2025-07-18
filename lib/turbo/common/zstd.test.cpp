/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include <turbo/common/test.hpp>
#include "zstd.hpp"

using namespace turbo;

suite turbo_common_zstd_suite = [] {
    "turbo::common::zstd"_test = [] {
        static const uint8_vector orig { std::string_view { "some text\0\x11\xFE" } };
        uint8_vector compressed {};

        "compress"_test = [&] {
            zstd::compress(compressed, orig);
            expect(compressed.size() > 8_u);
        };
        "decompress"_test = [&] {
            uint8_vector out {};
            zstd::decompress(out, compressed);
            expect(out == orig);
        };
        "decompressed_size"_test = [] {
            static std::string_view test_data { "Hello, world!" };
            auto compressed = zstd::compress(test_data, 3);
            expect(zstd::decompressed_size(compressed) == test_data.size());
        };
        "compress/decompress empty"_test = [] {
            static std::string_view empty { "" };
            auto compressed = zstd::compress(empty);
            expect(compressed.size() > 8_u);
            expect(zstd::decompressed_size(compressed) == 0_ull);
            auto decompressed = zstd::decompress(compressed);
            expect(decompressed.size() == 0_ull);
        };
        "file compress/decompress"_test = [] {
            uint8_vector raw {};
            for (size_t i = 0; i < 1000000; ++i)
                raw << buffer::from(i);
            auto compressed = zstd::compress(raw, 1);
            auto decompressed = zstd::decompress(compressed);
            expect(decompressed == raw);
        };
        "errors"_test = [&] {
            uint8_vector out {};
            compressed.clear();
            expect(throws([&] { zstd::decompress(out, compressed); }));
            uint64_t orig_size = 0;
            compressed.resize(9);
            memcpy(compressed.data(), &orig_size, sizeof(orig_size));
            expect(throws([&] { zstd::decompress(out, compressed); }));
            zstd::compress(compressed, orig);
            *reinterpret_cast<uint64_t *>(compressed.data()) = orig.size() + 10;
            expect(throws([&] { zstd::decompress(out, compressed); }));
            *reinterpret_cast<uint64_t *>(compressed.data()) = orig.size();
             byte_array<11> buf_too_small {};
            expect(throws([&] { zstd::decompress(buf_too_small, compressed); }));
             byte_array<13> buf_too_big {};
            expect(throws([&] { zstd::decompress(buf_too_big, compressed); }));
        };
    };
};