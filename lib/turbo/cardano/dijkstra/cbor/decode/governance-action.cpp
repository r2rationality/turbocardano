/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/transaction.hpp>

namespace turbo::cardano::dijkstra {
    namespace {
        protocol_version protocol_version_from_cbor(cbor::zero2::value &v)
        {
            auto &it = v.array();
            protocol_version res { it.read().uint(), numeric_cast<uint32_t>(it.read().uint()) };
            if (res.major > 13) [[unlikely]]
                throw error(fmt::format("unsupported Dijkstra protocol major version: {}", res.major));
            if (!it.done()) [[unlikely]]
                throw error("unexpected trailing Dijkstra protocol version elements");
            return res;
        }
    }

    governance_action_t governance_action_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        const auto type = it.read().uint();
        gov_action_t action = [&]() -> gov_action_t {
            switch (type) {
                case 0: return {{ gov_action_t::parameter_change_t {
                    optional_gov_action_id_t::from_cbor(it.read()),
                    protocol_param_update_t::from_cbor(it.read()).value,
                    optional_script_t::from_cbor(it.read())
                } }};
                case 1: return {{ gov_action_t::hard_fork_init_t {
                    optional_gov_action_id_t::from_cbor(it.read()),
                    protocol_version_from_cbor(it.read())
                } }};
                case 2: return {{ gov_action_t::treasury_withdrawals_t::from_cbor(it) }};
                case 3: return {{ gov_action_t::no_confidence_t::from_cbor(it) }};
                case 4: return {{ gov_action_t::update_committee_t::from_cbor(it) }};
                case 5: return {{ gov_action_t::new_constitution_t::from_cbor(it) }};
                case 6: return {{ gov_action_t::info_action_t::from_cbor(it) }};
                [[unlikely]] default:
                    throw error(fmt::format("unsupported Dijkstra governance action type: {}", type));
            }
        }();
        if (!it.done()) [[unlikely]]
            throw error(fmt::format("Dijkstra governance action type {} has trailing elements", type));
        return { std::move(action) };
    }
}
