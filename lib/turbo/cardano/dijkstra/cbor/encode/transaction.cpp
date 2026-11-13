/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

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
        _encode_contents(enc);
    }

    void tx::to_cbor_for_block(era_encoder &enc) const
    {
        enc.array(4);
        _encode_contents(enc);
        enc.boolean(!invalid());
    }

    void tx::_encode_contents(era_encoder &enc) const
    {
        enc.raw_cbor(_body.raw);
        enc.raw_cbor(witness_raw());
        if (!_auxiliary_data_raw.empty())
            enc.raw_cbor(_auxiliary_data_raw);
        else
            enc.s_null();
    }
}
