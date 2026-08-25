/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/shelley/block.hpp>
#include <turbo/cardano/shelley/cbor/decode/protocol-parameter.hpp>

namespace turbo::cardano::shelley {
    protocol_param_update_t protocol_param_update_t::from_cbor(cbor::zero2::value &v)
    {
        protocol_param_update_t res {};
        auto &it = v.map();
        while (!it.done()) {
            auto &key = it.read_key();
            const auto id = key.uint();
            auto &value = it.read_val(std::move(key));
            if (!detail::protocol_param_update_from_cbor<true, true>(res.value, id, value)) [[unlikely]]
                throw error(fmt::format("unsupported Shelley protocol parameter: {}", id));
        }
        return res;
    }

    update_t update_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        flat_map<key_hash, param_update> proposals {};
        {
            auto &props = it.read();
            if (!props.indefinite()) [[likely]]
                proposals.reserve(props.special_uint());
            auto &props_it = props.map();
            while (!props_it.done()) {
                auto &k = props_it.read_key();
                const auto vk = k.bytes();
                proposals.emplace_hint(
                    proposals.end(),
                    vk,
                    std::move(protocol_param_update_t::from_cbor(props_it.read_val(std::move(k))).value)
                );
            }
        }
        const auto epoch = it.read().uint();
        update_t res {};
        res.reserve(proposals.size());
        for (auto &&[vk, upd]: proposals)
            res.emplace_back(vk, std::move(upd), epoch);
        return res;
    }
}
