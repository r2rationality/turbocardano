/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/conway/transaction.hpp>

namespace turbo::cardano::conway {
    value_t value_t::from_cbor(cbor::zero2::value &v)
    {
        if (v.type() == cbor::major_type::uint)
            return {{ v.uint() }};
        if (v.type() != cbor::major_type::array) [[unlikely]]
            throw error(fmt::format("unsupported output value type: {}", v.type()));

        auto &it = v.array();
        const auto coin = it.read().uint();
        auto &assets_v = it.read();
        multi_asset_map assets {};
        if (!assets_v.indefinite()) [[likely]]
            assets.reserve(assets_v.special_uint());
        auto &assets_it = assets_v.map();
        while (!assets_it.done()) {
            auto &policy_id_v = assets_it.read_key();
            const auto policy_id = policy_id_v.bytes();
            auto &policy_assets_v = assets_it.read_val(std::move(policy_id_v));
            policy_asset_map policy_assets {};
            if (!policy_assets_v.indefinite()) [[likely]]
                policy_assets.reserve(policy_assets_v.special_uint());
            auto &policy_assets_it = policy_assets_v.map();
            while (!policy_assets_it.done()) {
                auto &name_v = policy_assets_it.read_key();
                const auto name = name_v.bytes();
                const auto amount = policy_assets_it.read_val(std::move(name_v)).uint();
                if (!amount) [[unlikely]]
                    throw error("Conway asset amounts must be positive");
                policy_assets.emplace_hint(policy_assets.end(), name, amount);
            }
            if (policy_assets.empty()) [[unlikely]]
                throw error("a multiasset policy must contain at least one asset");
            assets.emplace_hint(assets.end(), policy_id, std::move(policy_assets));
        }
        if (!it.done()) [[unlikely]]
            throw error("unexpected trailing value elements");
        return {{ coin, std::move(assets) }};
    }
}
