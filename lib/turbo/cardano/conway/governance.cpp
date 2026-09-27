/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/cert.hpp>

namespace turbo::cardano {
    anchor_t anchor_t::from_json(const json::value &j)
    {
        return { json::value_to<std::string>(j.at("url")), datum_hash::from_hex(j.at("dataHash").as_string()) };
    }

    constitution_t constitution_t::from_json(const json::value &j)
    {
        optional_script_t policy_id {};
        const auto &obj = j.as_object();
        const auto script_it = obj.find("script");
        if (script_it != obj.end() && !script_it->value().is_null())
            policy_id.emplace(script_hash::from_hex(script_it->value().as_string()));
        return {
            anchor_t::from_json(obj.at("anchor")),
            std::move(policy_id)
        };
    }

#include <turbo/cardano/ledger/rules/ratify/action-order.ipp>

}
