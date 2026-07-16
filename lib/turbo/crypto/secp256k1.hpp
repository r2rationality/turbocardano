#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

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