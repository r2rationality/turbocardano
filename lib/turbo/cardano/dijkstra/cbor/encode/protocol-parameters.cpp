/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/transaction.hpp>

namespace turbo::cardano::dijkstra {
    void protocol_param_update_t::to_cbor(era_encoder &enc) const
    {
        value.to_cbor(enc);
    }
}
