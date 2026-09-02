/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/block.hpp>

namespace turbo::cardano::dijkstra {
    void leios_certificate_t::to_cbor(era_encoder &enc) const
    {
        enc.array(2).bytes(signers).bytes(signature);
    }

    void block_transactions_t::to_cbor(era_encoder &enc) const
    {
        enc.array_compact(txs.size(), [&] {
            for (const auto &transaction: txs)
                transaction.to_cbor(enc);
        });
    }

    void block::to_cbor(era_encoder &enc) const
    {
        enc.array(2);
        _header.to_cbor(enc);
        enc.array(4);
        if (_invalid_transactions.empty())
            enc.s_null();
        else
            _invalid_transactions.to_cbor(enc);
        _transactions->to_cbor(enc);
        if (_leios_certificate)
            _leios_certificate->to_cbor(enc);
        else
            enc.s_null();
        if (_peras_certificate)
            enc.bytes(*_peras_certificate);
        else
            enc.s_null();
    }
}
