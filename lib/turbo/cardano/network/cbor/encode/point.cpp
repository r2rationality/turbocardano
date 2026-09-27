/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/types.hpp>

namespace turbo::cardano {
    void point2::to_cbor(cbor::encoder &enc) const
    {
        enc.array(2);
        enc.uint(slot);
        enc.bytes(hash);
    }

    void point3::to_cbor(cbor::encoder &enc) const
    {
        enc.array(2);
        point2::to_cbor(enc);
        enc.uint(height);
    }

    void optional_point2::to_cbor(cbor::encoder &enc) const
    {
        if (has_value())
            value().to_cbor(enc);
        else
            enc.array(0);
    }

    void optional_point3::to_cbor(cbor::encoder &enc) const
    {
        enc.array(2);
        optional_point2::to_cbor(enc);
        enc.uint(has_value() ? height : 0);
    }
}
