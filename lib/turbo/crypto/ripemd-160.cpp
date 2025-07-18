/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <botan/hash.h>
#include <turbo/crypto/ripemd-160.hpp>

namespace turbo::crypto::ripemd_160 {
    void digest(const std::span<uint8_t> &out, const buffer &in)
    {
        const auto h = Botan::HashFunction::create_or_throw("RIPEMD-160");
        if (h->output_length() != out.size()) [[unlikely]]
            throw error(fmt::format("RIPEMD-160: expects an output buffer of size: {} but got: {}", h->output_length(), out.size()));
        h->update(in.data(), in.size());
        h->final(out.data());
    }
}