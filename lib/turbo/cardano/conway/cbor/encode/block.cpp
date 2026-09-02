/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/conway/block.hpp>

namespace turbo::cardano::conway {
    void block_transactions_t::bodies_to_cbor(era_encoder &enc) const
    {
        enc.array_compact(txs.size(), [&] {
            for (const auto &transaction: txs)
                transaction.body_to_cbor(enc);
        });
    }

    void block_transactions_t::witnesses_to_cbor(era_encoder &enc) const
    {
        enc.array_compact(txs.size(), [&] {
            for (const auto &transaction: txs)
                transaction.witnesses_to_cbor(enc);
        });
    }

    void block::to_cbor(era_encoder &enc) const
    {
        enc.array(5);
        _hdr.to_cbor(enc);
        _txs.bodies_to_cbor(enc);
        _txs.witnesses_to_cbor(enc);
        _meta.to_cbor(enc);
        _invalid_txs.to_cbor(enc);
    }
}
