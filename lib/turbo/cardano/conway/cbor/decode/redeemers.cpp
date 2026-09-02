/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/conway/transaction.hpp>

namespace turbo::cardano::conway {
    redeemers_t redeemers_t::from_cbor(cbor::zero2::value &v)
    {
        redeemers_t res {};
        switch (const auto type = v.type(); type) {
            case cbor::major_type::array: {
                auto &it = v.array();
                if (!v.indefinite()) [[likely]]
                    res.items.reserve(v.special_uint());
                while (!it.done())
                    res.add(std::move(redeemer_t::from_cbor(it.read()).value));
                break;
            }
            case cbor::major_type::map: {
                auto &it = v.map();
                if (!v.indefinite()) [[likely]]
                    res.items.reserve(v.special_uint());
                while (!it.done())
                    res.add(std::move(redeemer_t::from_cbor(it).value));
                break;
            }
            [[unlikely]] default: throw error(fmt::format("unsupported redeemers type: {}", type));
        }
        if (res.items.empty()) [[unlikely]]
            throw error("Conway redeemers must be nonempty when supplied");
        res.raw = v.data_raw();
        return res;
    }
}
