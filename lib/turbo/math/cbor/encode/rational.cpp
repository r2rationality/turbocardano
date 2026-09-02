/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cbor/encoder.hpp>
#include <turbo/math/rational.hpp>

namespace turbo {
    void rational_u64::to_cbor(cbor::encoder &enc) const
    {
        enc.tag(30);
        enc.array(2)
            .uint(numerator)
            .uint(denominator);
    }
}
