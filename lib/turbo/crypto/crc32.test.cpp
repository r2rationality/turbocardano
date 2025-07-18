/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

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
};