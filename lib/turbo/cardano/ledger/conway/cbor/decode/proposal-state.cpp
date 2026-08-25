/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/cert.hpp>

namespace turbo::cardano {
    // Ledger-state proposal wrapper decoder.
    proposal_t proposal_t::from_cbor(const gov_action_id_t &id_, cbor::zero2::value &v)
    {
        return { id_, proposal_procedure_t::from_cbor(v) };
    }
}
