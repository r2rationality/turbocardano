/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/types.hpp>
#include <turbo/cbor/zero2.hpp>

namespace turbo::cardano {
    using namespace crypto;

    credential_t credential_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        const auto type = it.read().uint();
        if (type > 1) [[unlikely]]
            throw error(fmt::format("unsupported credential type: {}", type));
        credential_t res { it.read().bytes(), type == 1 };
        if (!it.done()) [[unlikely]]
            throw error("unexpected trailing credential elements");
        return res;
    }
}
