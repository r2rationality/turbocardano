#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/util.hpp>

namespace turbo::crypto::secp256k1
{
    namespace ecdsa {
        extern bool verify(const buffer &sig, const buffer &vk, const buffer &msg);
    }

    namespace schnorr {
        extern bool verify(const buffer &sig, const buffer &vk, const buffer &msg);
    }
}