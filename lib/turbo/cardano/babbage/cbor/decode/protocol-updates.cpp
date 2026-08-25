/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/babbage/block.hpp>

namespace turbo::cardano::babbage {
    update_t update_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        flat_map<key_hash, param_update> proposals {};
        auto &proposals_v = it.read();
        if (!proposals_v.indefinite()) [[likely]]
            proposals.reserve(proposals_v.special_uint());
        auto &proposals_it = proposals_v.map();
        while (!proposals_it.done()) {
            auto &key = proposals_it.read_key();
            const auto verification_key = key.bytes();
            auto &value = proposals_it.read_val(std::move(key));
            proposals.emplace_hint(
                proposals.end(),
                verification_key,
                std::move(protocol_param_update_t::from_cbor(value).value)
            );
        }
        const auto epoch = it.read().uint();
        if (!it.done()) [[unlikely]]
            throw error("unexpected trailing update elements");
        update_t res {};
        res.value.reserve(proposals.size());
        for (auto &&[key, update]: proposals)
            res.value.emplace_back(key, std::move(update), epoch);
        return res;
    }
}
