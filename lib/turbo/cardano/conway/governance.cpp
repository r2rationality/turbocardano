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

    void anchor_t::to_cbor(era_encoder &enc) const
    {
        enc.array(2).text(url).bytes(hash);
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

    void constitution_t::to_cbor(era_encoder &enc) const
    {
        enc.array(2);
        anchor.to_cbor(enc);
        policy_id.to_cbor(enc);
    }

    void gov_action_t::parameter_change_t::to_cbor(era_encoder &enc) const
    {
        enc.array(4);
        enc.uint(0);
        prev_action_id.to_cbor(enc);
        update.to_cbor(enc);
        policy_id.to_cbor(enc);
    }

    void gov_action_t::hard_fork_init_t::to_cbor(era_encoder &enc) const
    {
        enc.array(3);
        enc.uint(1);
        prev_action_id.to_cbor(enc);
        protocol_ver.to_cbor(enc);
    }

    void gov_action_t::treasury_withdrawals_t::to_cbor(era_encoder &enc) const
    {
        enc.array(3);
        enc.uint(2);
        enc.map_compact(withdrawals.size(), [&] {
            for (const auto &[reward_id, coin]: withdrawals) {
                enc.bytes(reward_id);
                enc.uint(coin);
            }
        });
        policy_id.to_cbor(enc);
    }

    void gov_action_t::no_confidence_t::to_cbor(era_encoder &enc) const
    {
        enc.array(2);
        enc.uint(3);
        prev_action_id.to_cbor(enc);
    }

    void gov_action_t::update_committee_t::to_cbor(era_encoder &enc) const
    {
        enc.array(5);
        enc.uint(4);
        prev_action_id.to_cbor(enc);
        enc.tag(258);
        enc.array_compact(members_to_remove.size(), [&] {
            const auto encode_removals = [&](const bool script) {
                for (const auto &id: members_to_remove) {
                    if (id.script == script)
                        id.to_cbor(enc);
                }
            };
            encode_removals(true);
            encode_removals(false);
        });
        enc.map_compact(members_to_add.size(), [&] {
            const auto encode_additions = [&](const bool script) {
                for (const auto &[id, epoch]: members_to_add) {
                    if (id.script == script) {
                        id.to_cbor(enc);
                        enc.uint(epoch);
                    }
                }
            };
            encode_additions(true);
            encode_additions(false);
        });
        new_threshold.to_cbor(enc);
    }

    void gov_action_t::new_constitution_t::to_cbor(era_encoder &enc) const
    {
        enc.array(3);
        enc.uint(5);
        prev_action_id.to_cbor(enc);
        new_constitution.to_cbor(enc);
    }

    void gov_action_t::info_action_t::to_cbor(era_encoder &enc) const
    {
        enc.array(1);
        enc.uint(6);
    }

    void gov_action_t::to_cbor(era_encoder &enc) const
    {
        std::visit([&](const auto &v) {
            v.to_cbor(enc);
        }, val);
    }

    bool gov_action_t::delaying() const
    {
        return std::visit<bool>([&](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, no_confidence_t>)
                return true;
            if constexpr (std::is_same_v<T, hard_fork_init_t>)
                return true;
            if constexpr (std::is_same_v<T, update_committee_t>)
                return true;
            if constexpr (std::is_same_v<T, new_constitution_t>)
                return true;
            if constexpr (std::is_same_v<T, treasury_withdrawals_t>)
                return false;
            if constexpr (std::is_same_v<T, parameter_change_t>)
                return false;
            if constexpr (std::is_same_v<T, info_action_t>)
                return false;
            throw error(fmt::format("unsupported gov_action: {}", typeid(T).name()));
            return false;
        }, val);
    }

    int gov_action_t::priority() const
    {
        return std::visit<int>([&](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, no_confidence_t>)
                return 0;
            if constexpr (std::is_same_v<T, update_committee_t>)
                return 1;
            if constexpr (std::is_same_v<T, new_constitution_t>)
                return 2;
            if constexpr (std::is_same_v<T, hard_fork_init_t>)
                return 3;
            if constexpr (std::is_same_v<T, parameter_change_t>)
                return 4;
            if constexpr (std::is_same_v<T, treasury_withdrawals_t>)
                return 5;
            if constexpr (std::is_same_v<T, info_action_t>)
                return 6;
            throw error(fmt::format("unsupported gov_action: {}", typeid(T).name()));
            return std::numeric_limits<int>::max();
        }, val);
    }

    std::strong_ordering gov_action_t::operator<=>(const gov_action_t &o) const
    {
        return priority() <=> o.priority();
    }
}
