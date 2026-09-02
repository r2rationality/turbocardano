/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/types.hpp>

namespace turbo::cardano {
    output_value_t output_value_t::from_cbor(cbor::zero2::value &v)
    {
        switch (const auto type = v.type(); type) {
            case cbor::major_type::uint: return { v.uint() };
            case cbor::major_type::array: {
                auto &it = v.array();
                const auto coin = it.read().uint();
                auto &assets_v = it.read();
                auto &assets_it = assets_v.map();
                multi_asset_map assets {};
                if (!assets_v.indefinite()) [[likely]]
                    assets.reserve(assets_v.special_uint());
                while (!assets_it.done()) {
                    auto &policy_id_v = assets_it.read_key();
                    const auto policy_id = policy_id_v.bytes();
                    auto &policy_assets_v = assets_it.read_val(std::move(policy_id_v));
                    auto &policy_assets_it = policy_assets_v.map();
                    policy_asset_map policy_assets {};
                    if (!policy_assets_v.indefinite()) [[likely]]
                        policy_assets.reserve(policy_assets_v.special_uint());
                    while (!policy_assets_it.done()) {
                        auto &name_v = policy_assets_it.read_key();
                        const auto name = name_v.bytes();
                        if (const auto amount = policy_assets_it.read_val(std::move(name_v)).uint(); amount) [[likely]]
                            policy_assets.emplace_hint(policy_assets.end(), name, amount);
                    }
                    if (!policy_assets.empty()) [[likely]]
                        assets.emplace_hint(assets.end(), policy_id, std::move(policy_assets));
                }
                if (!it.done()) [[unlikely]]
                    throw error("unexpected trailing value elements");
                return { coin, std::move(assets) };
            }
            [[unlikely]] default: throw error(fmt::format("unsupported output value type: {}", type));
        }
    }
}
