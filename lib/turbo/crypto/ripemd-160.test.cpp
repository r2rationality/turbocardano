/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/common/test.hpp>
#include <turbo/crypto/ripemd-160.hpp>

using namespace turbo;
using namespace turbo::crypto::ripemd_160;

suite crypto_ripemd_160_suite = [] {
    "crypto::ripemd_160"_test = [] {
        using test_vector = std::pair<std::string, uint8_vector>;
        static std::vector<test_vector> test_vectors = {
            test_vector { "9c1185a5c5e9fc54612808977ee8f548b2258d31", uint8_vector::from_hex("") },
            test_vector { "8eb208f7e05d987a9b044a8e98c6b087f15a0bfc", uint8_vector { std::string_view { "abc" } } }
        };
        for (const auto &[exp_hash, input]: test_vectors) {
            const auto exp_hash_bin = uint8_vector::from_hex(exp_hash);
            const auto hash = digest(input);
            expect_equal(hash, exp_hash_bin);
        }
    };
};