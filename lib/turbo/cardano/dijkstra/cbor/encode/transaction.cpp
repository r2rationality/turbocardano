/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/cbor/encode/transaction-witness-set.hpp>
#include <turbo/cardano/dijkstra/transaction.hpp>

namespace turbo::cardano::dijkstra {
    void transaction_t::to_cbor(era_encoder &enc) const
    {
        enc.array(3);
        body.to_cbor(enc);
        witnesses.to_cbor(enc);
        if (auxiliary_data)
            auxiliary_data->to_cbor(enc);
        else
            enc.s_null();
    }

    void sub_transaction_t::to_cbor(era_encoder &enc) const
    {
        enc.array(3);
        body.to_cbor(enc);
        witnesses.to_cbor(enc);
        if (auxiliary_data)
            auxiliary_data->to_cbor(enc);
        else
            enc.s_null();
    }

    void tx::to_cbor(era_encoder &enc) const
    {
        enc.array(3);
        _body.to_cbor(enc);
        detail::transaction_witness_set_to_cbor(enc, _wits, _redeemers);
        if (_auxiliary_data)
            _auxiliary_data->to_cbor(enc);
        else
            enc.s_null();
    }
}
