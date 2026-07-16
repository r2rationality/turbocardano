/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/test.hpp>
#include <turbo/common/timer.hpp>
#include <turbo/crypto/blake2b.hpp>
#include <turbo/file.hpp>

namespace {
    using namespace turbo;
    using namespace turbo::crypto;
}

suite blake2b_suite = [] {
    "blake2b"_test = [] {
        using test_vector = std::pair<std::string, uint8_vector>;
        static std::vector<test_vector> test_vectors = {
            test_vector { "0e5751c026e543b2e8ab2eb06099daa1d1e5df47778f7787faab45cdf12fe3a8", uint8_vector {} },
            test_vector { "47F62675C9B0161211B9261B7BB1CF801EDD4B9C0728D9A6C7A910A1581EED41",
                zstd::read("./data/chunk-registry/compressed/chunk/47F62675C9B0161211B9261B7BB1CF801EDD4B9C0728D9A6C7A910A1581EED41.zstd") }
        };
        for (const auto &[exp_hash, input]: test_vectors) {
            const auto exp_hash_bin = uint8_vector::from_hex(exp_hash);
            const auto hash = blake2b::digest(input);
            expect_equal(static_cast<buffer>(exp_hash_bin), hash);
        }
    };
};