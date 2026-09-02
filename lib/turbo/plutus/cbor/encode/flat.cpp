/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cbor/encoder.hpp>
#include <turbo/plutus/flat-encoder.hpp>

namespace turbo::plutus::flat {
    uint8_vector encode_cbor(const version &v, const term &t)
    {
        cbor::encoder enc {};
        enc.bytes(encode(v, t));
        return std::move(enc.cbor());
    }
}
