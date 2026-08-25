/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/types.hpp>

namespace turbo::cardano {
    datum_option_t datum_option_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        datum_option_t res { datum_hash {} };
        switch (const auto id = it.read().uint(); id) {
            case 0: res.val = datum_hash { it.read().bytes() }; break;
            case 1: {
                auto &tag = it.read().tag();
                if (tag.id() != 24) [[unlikely]]
                    throw error(fmt::format("expected a tag with id 24 but got: {}", tag.id()));
                res.val = uint8_vector { tag.read().bytes() };
                break;
            }
            default: throw error(fmt::format("unsupported datum_option id: {}", id));
        }
        if (!it.done()) [[unlikely]]
            throw error("unexpected trailing datum_option elements");
        return res;
    }
}
