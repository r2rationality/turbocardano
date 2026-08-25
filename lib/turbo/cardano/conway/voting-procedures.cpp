/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/cert.hpp>

namespace turbo::cardano {
    void gov_action_id_t::to_cbor(era_encoder &enc) const
    {
        enc.array(2)
            .bytes(tx_id)
            .uint(idx);
    }

    void voting_procedure_t::to_cbor(era_encoder &enc) const
    {
        switch (vote) {
            case vote_t::no: enc.uint(0); break;
            case vote_t::yes: enc.uint(1); break;
            case vote_t::abstain: enc.uint(2); break;
            default: throw error(fmt::format("unsupported vote: {}", static_cast<int>(vote)));
        }
    }
}
