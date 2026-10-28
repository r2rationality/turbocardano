/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/transaction.hpp>

namespace turbo::cardano::dijkstra {
    voting_procedure_t voting_procedure_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        const auto vote = it.read().uint();
        if (vote > static_cast<uint64_t>(vote_t::abstain)) [[unlikely]]
            throw error{"unsupported vote: {}", vote};
        voting_procedure_t res {{ static_cast<decltype(res.vote)>(vote), optional_anchor_t::from_cbor(it.read()) }};
        if (!it.done()) [[unlikely]]
            throw error("unexpected trailing Dijkstra voting procedure elements");
        return res;
    }

    voting_procedures_t voting_procedures_t::from_cbor(cbor::zero2::value &v)
    {
        voting_procedures_t res {};
        if (!v.indefinite()) [[likely]]
            res.reserve(v.special_uint());
        auto &it = v.map();
        while (!it.done()) {
            auto &key = it.read_key();
            auto voter = voter_t::from_cbor(key);
            auto &val = it.read_val(std::move(key));
            if (!val.indefinite()) [[likely]]
                res.reserve(res.size() + val.special_uint());
            auto &v_it = val.map();
            while (!v_it.done()) {
                auto &v_key = v_it.read_key();
                auto ga_id = gov_action_id_t::from_cbor(v_key);
                res.emplace_hint(res.end(), voter, std::move(ga_id), voting_procedure_t::from_cbor(v_it.read_val(std::move(v_key))));
            }
        }
        return res;
    }
}
