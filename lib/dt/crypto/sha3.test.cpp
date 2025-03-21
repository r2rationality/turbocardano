/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/common/test.hpp>
#include <dt/crypto/sha3.hpp>

using namespace daedalus_turbo;
using namespace daedalus_turbo::crypto;

suite crypto_sha3_suite = [] {
    "crypto::sha3"_test = [] {
        using test_vector = std::pair<std::string, uint8_vector>;
        static std::vector<test_vector> test_vectors = {
                test_vector { "a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a", uint8_vector::from_hex("") },
                test_vector { "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532", uint8_vector { std::string_view { "abc" } } }
        };
        for (const auto &[exp_hash, input]: test_vectors) {
            const auto exp_hash_bin = uint8_vector::from_hex(exp_hash);
            const auto hash = sha3::digest(input);
            expect(hash == exp_hash_bin) << hash;
        }
    };
};