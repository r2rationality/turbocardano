/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/cert.hpp>

namespace turbo::cardano {
    anchor_t anchor_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return { std::string { it.read().text() }, it.read().bytes() };
    }

    constitution_t constitution_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return { decltype(anchor)::from_cbor(it.read()), decltype(policy_id)::from_cbor(it.read()) };
    }

    gov_action_t::parameter_change_t gov_action_t::parameter_change_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return {
            optional_gov_action_id_t::from_cbor(it.read()),
            param_update_t::from_cbor(it.read()),
            optional_script_t::from_cbor(it.read())
        };
    }

    gov_action_t::hard_fork_init_t gov_action_t::hard_fork_init_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return {
            optional_gov_action_id_t::from_cbor(it.read()),
            protocol_version::from_cbor(it.read())
        };
    }

    gov_action_t::treasury_withdrawals_t gov_action_t::treasury_withdrawals_t::from_cbor(cbor::zero2::array_reader &it)
    {
        withdrawal_map withdrawals {};
        {
            auto &wdh = it.read();
            if (!wdh.indefinite()) [[likely]]
                withdrawals.reserve(wdh.special_uint());
            auto &w_it = wdh.map();
            while (!w_it.done()) {
                auto &key = w_it.read_key();
                const auto addr = key.bytes();
                auto &coin = w_it.read_val(std::move(key));
                withdrawals.emplace_hint(withdrawals.end(), addr, coin.uint());
            }
        }
        return {
            std::move(withdrawals),
            optional_script_t::from_cbor(it.read())
        };
    }

    gov_action_t::no_confidence_t gov_action_t::no_confidence_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return { optional_gov_action_id_t::from_cbor(it.read()) };
    }

    gov_action_t::update_committee_t gov_action_t::update_committee_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return {
            decltype(prev_action_id)::from_cbor(it.read()),
            decltype(members_to_remove)::from_cbor(it.read()),
            decltype(members_to_add)::from_cbor(it.read()),
            decltype(new_threshold)::from_cbor(it.read())
        };
    }

    gov_action_t::new_constitution_t gov_action_t::new_constitution_t::from_cbor(cbor::zero2::array_reader &it)
    {
        return {
            optional_gov_action_id_t::from_cbor(it.read()),
            constitution_t::from_cbor(it.read())
        };
    }

    gov_action_t::info_action_t gov_action_t::info_action_t::from_cbor(cbor::zero2::array_reader &)
    {
        return {};
    }

    static gov_action_t::value_type gov_action_t_from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        switch (const auto typ = it.read().uint(); typ) {
            case 0: return gov_action_t::parameter_change_t::from_cbor(it);
            case 1: return gov_action_t::hard_fork_init_t::from_cbor(it);
            case 2: return gov_action_t::treasury_withdrawals_t::from_cbor(it);
            case 3: return gov_action_t::no_confidence_t::from_cbor(it);
            case 4: return gov_action_t::update_committee_t::from_cbor(it);
            case 5: return gov_action_t::new_constitution_t::from_cbor(it);
            case 6: return gov_action_t::info_action_t::from_cbor(it);
            [[unlikely]] default: throw error(fmt::format("unsupported gov action type: {}", typ));
        }
    }

    gov_action_t gov_action_t::from_cbor(cbor::zero2::value &v)
    {
        return { gov_action_t_from_cbor(v) };
    }
}
