/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/test.hpp>
#include <turbo/crypto/crc32.hpp>

using namespace turbo;
using namespace turbo::crypto;

suite crypto_crc32_suite = [] {
    "crypto::crc32"_test = [] {
        using test_vector = std::pair<crc32::hash_32, std::string_view>;
        static std::vector<test_vector> test_vectors = {
            test_vector { 0, "" },
            test_vector { 0xcbf43926, "123456789" },
            test_vector { 0x190a55ad, { "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 32 } }
        };
        for (const auto &[exp_hash, input]: test_vectors) {
            const auto hash = crc32::digest(input);
            expect(hash == exp_hash) << buffer { input } << fmt::format("{:08X}", hash);
        }
    };
    "crypto::crc32 unaligned input"_test = [] {
        uint8_vector storage(257, 0xA5);
        const buffer aligned { storage.data(), 256 };
        const buffer unaligned { storage.data() + 1, 256 };
        expect_equal(crc32::digest(unaligned), crc32::digest(aligned));
    };
};
