/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/conway/transaction.hpp>

namespace turbo::cardano::conway {
    certificates_t certificates_t::from_cbor(cbor::zero2::value &v)
    {
        certificates_t res {};
        auto *items = &v;
        if (v.type() == cbor::major_type::tag) {
            auto &reader = v.tag();
            if (reader.id() != 258) [[unlikely]]
                throw error(fmt::format("expected a tag with id 258 but got: {}!", reader.id()));
            items = &reader.read();
        }
        if (!items->indefinite()) [[likely]]
            res.reserve(items->special_uint());
        auto &it = items->array();
        while (!it.done())
            res.emplace_back(std::move(certificate_t::from_cbor(it.read()).value));
        return res;
    }
}
