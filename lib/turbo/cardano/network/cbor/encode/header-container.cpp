/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano.hpp>

namespace turbo::cardano {
    void parsed_header::to_cbor(cbor::encoder &enc) const
    {
        if (hdr->era() <= 1) {
            enc.array(2)
                .uint(0)
                .array(2)
                    .array(2)
                        .uint(hdr->era())
                        .uint(data.size())
                    .tag(24)
                        .bytes(hdr->data_raw());
        } else {
            enc.array(2)
                .uint(hdr->era() - 1)
                .tag(24)
                    .bytes(hdr->data_raw());
        }
    }
}
