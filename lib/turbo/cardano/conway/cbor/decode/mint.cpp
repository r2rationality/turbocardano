/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/conway/transaction.hpp>
#include <turbo/cardano/mary/cbor/decode/mint.hpp>

namespace turbo::cardano::conway {
    namespace {
        [[noreturn]] void reject_invalid_mint(const mary::detail::invalid_mint issue)
        {
            if (issue == mary::detail::invalid_mint::zero_amount)
                throw error("Conway mint amounts must be nonzero");
            throw error("a Conway mint policy must contain at least one asset");
        }
    }

    mint_t mint_t::from_cbor(cbor::zero2::value &v)
    {
        mint_t res {};
        mary::detail::decode_mint(v, res, reject_invalid_mint);
        return res;
    }
}
