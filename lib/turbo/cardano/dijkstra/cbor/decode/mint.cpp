/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/transaction.hpp>
#include <turbo/cardano/mary/cbor/decode/mint.hpp>

namespace turbo::cardano::dijkstra {
    namespace {
        void invalid_mint(const mary::detail::invalid_mint invalid)
        {
            switch (invalid) {
                case mary::detail::invalid_mint::zero_amount:
                    throw error("Dijkstra mint amounts must be nonzero");
                case mary::detail::invalid_mint::empty_policy:
                    throw error("Dijkstra mint policy maps must be nonempty");
            }
            std::unreachable();
        }
    }

    mint_t mint_t::from_cbor(cbor::zero2::value &v)
    {
        mint_t result {};
        mary::detail::decode_mint(v, result, invalid_mint);
        if (result.empty()) [[unlikely]]
            throw error("Dijkstra mint maps must be nonempty");
        return result;
    }
}
