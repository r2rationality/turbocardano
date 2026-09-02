/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/types.hpp>

namespace turbo::cardano {
    void address::to_cbor(era_encoder &enc) const
    {
        if (is_byron() && _bytes[0] == 0x83) {
            enc.bytes(byron_crc_protected(bytes()));
        } else {
            enc.bytes(bytes());
        }
    }
}

