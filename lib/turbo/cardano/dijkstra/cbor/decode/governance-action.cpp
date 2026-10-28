/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/transaction.hpp>

namespace turbo::cardano::dijkstra {
    anchor_t anchor_t::from_cbor(cbor::zero2::value &v)
    {
        return { cardano::anchor_t::from_cbor(v.array()) };
    }

    optional_anchor_t optional_anchor_t::from_cbor(cbor::zero2::value &v)
    {
        optional_anchor_t res {};
        if (!v.is_null())
            res.emplace(anchor_t::from_cbor(v));
        return res;
    }

    gov_action_id_t gov_action_id_t::from_cbor(cbor::zero2::value &v)
    {
        return { cardano::gov_action_id_t::from_cbor(v.array()) };
    }

    optional_gov_action_id_t optional_gov_action_id_t::from_cbor(cbor::zero2::value &v)
    {
        optional_gov_action_id_t res {};
        if (!v.is_null())
            res.emplace(gov_action_id_t::from_cbor(v));
        return res;
    }

    constitution_t constitution_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        constitution_t res {{ anchor_t::from_cbor(it.read()), decltype(policy_id)::from_cbor(it.read()) }};
        if (!it.done()) [[unlikely]]
            throw error("unexpected trailing Dijkstra constitution elements");
        return res;
    }

    namespace {
        auto protocol_version_from_cbor(cbor::zero2::value &v)
        {
            decltype(gov_action_t::hard_fork_init_t::protocol_ver) res { cardano::protocol_version::from_cbor(v) };
            res.minor = numeric_cast<uint32_t>(res.minor);
            if (res.major > 13) [[unlikely]]
                throw error{"unsupported Dijkstra protocol major version: {}", res.major};
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
                case 3: return {{ gov_action_t::no_confidence_t {
                    optional_gov_action_id_t::from_cbor(it.read())
                } }};
                case 4: return {{ gov_action_t::update_committee_t {
                    optional_gov_action_id_t::from_cbor(it.read()),
                    decltype(gov_action_t::update_committee_t::members_to_remove)::from_cbor(it.read()),
                    decltype(gov_action_t::update_committee_t::members_to_add)::from_cbor(it.read()),
                    decltype(gov_action_t::update_committee_t::new_threshold)::from_cbor(it.read())
                } }};
                case 5: return {{ gov_action_t::new_constitution_t {
                    optional_gov_action_id_t::from_cbor(it.read()),
                    constitution_t::from_cbor(it.read())
                } }};
                case 6: return {{ gov_action_t::info_action_t::from_cbor(it) }};
                [[unlikely]] default:
                    throw error{"unsupported Dijkstra governance action type: {}", type};
            }
        }();
        if (!it.done()) [[unlikely]]
            throw error{"Dijkstra governance action type {} has trailing elements", type};
        return { std::move(action) };
    }
}
