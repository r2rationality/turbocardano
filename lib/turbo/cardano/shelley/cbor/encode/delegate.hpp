#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/types.hpp>

namespace turbo::cardano {
    void shelley_delegate::to_cbor(auto &enc) const
    {
        enc.array(2).bytes(delegate).bytes(vrf);
    }

    void future_shelley_delegate::to_cbor(auto &enc) const
    {
        enc.array(2).uint(slot).bytes(genesis);
    }
}
