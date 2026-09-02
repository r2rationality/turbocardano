/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include "transaction-body.hpp"
#include <turbo/cardano/alonzo/cbor/decode/full/transaction-body.hpp>
#include <turbo/cardano/conway/transaction.hpp>

namespace turbo::cardano::conway::full {
    transaction_body_t::transaction_body_t(const conway::transaction_body_t &body):
        value { body }
    {
        auto fields = alonzo::full::detail::transaction_body_raw_fields_from_cbor(body.raw);
        auxiliary_data_hash = std::move(fields.auxiliary_data_hash);
        script_data_hash = std::move(fields.script_data_hash);
        network_id = fields.network_id;
    }
}
