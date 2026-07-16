/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/test.hpp>
#include <turbo/crypto/blst.hpp>

using namespace turbo;

suite crypto_blst_suite = [] {
    "crypto::blst"_test = [] {
        uint64_t in[4] {1, 2, 3, 4};
        blst_scalar out {};
        blst_scalar_from_uint64(&out, in);
    };
};