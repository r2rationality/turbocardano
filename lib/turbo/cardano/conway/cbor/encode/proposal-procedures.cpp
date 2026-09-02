/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/cert.hpp>

namespace turbo::cardano {
    void proposal_procedure_t::to_cbor(era_encoder &enc) const
    {
        enc.array(4);
        enc.uint(deposit);
        byte_array<sizeof(return_addr.hash) + 1> stake_addr;
        stake_addr[0] = (return_addr.script ? 0xF0 : 0xE0) | (return_addr_network_id & 0xF);
        memcpy(stake_addr.data() + 1, return_addr.hash.data(), return_addr.hash.size());
        enc.bytes(stake_addr);
        action.to_cbor(enc);
        anchor.to_cbor(enc);
    }
}
