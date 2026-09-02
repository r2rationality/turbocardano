/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/transaction.hpp>
#include <turbo/plutus/types.hpp>

namespace turbo::cardano::dijkstra {
    void plutus_data_t::to_cbor(era_encoder &enc) const
    {
        plutus::allocator alloc {};
        to_cbor(enc, alloc);
    }

    void plutus_data_t::to_cbor(era_encoder &enc, plutus::allocator &alloc) const
    {
        plutus::data::from_cbor(alloc, raw).to_cbor(enc);
    }
}
