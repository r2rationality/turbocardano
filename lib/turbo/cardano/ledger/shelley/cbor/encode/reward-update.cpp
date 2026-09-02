/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/ledger/types.hpp>

namespace turbo::cardano::ledger {
    void reward_update::to_cbor(era_encoder &enc) const
    {
        enc.array(3)
            .uint(type == reward_type::leader ? 1 : 0)
            .bytes(pool_id)
            .uint(amount);
    }
}
