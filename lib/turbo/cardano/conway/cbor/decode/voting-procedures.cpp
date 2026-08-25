/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/conway/transaction.hpp>

namespace turbo::cardano {
    gov_action_id_t gov_action_id_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return { it.read().bytes(), numeric_cast<uint16_t>(it.read().uint()) };
    }

    voter_t::type_t voter_type_from_cbor(cbor::zero2::value &v)
    {
        switch (const auto typ = v.uint(); typ) {
            case 0: return voter_t::const_comm_key;
            case 1: return voter_t::const_comm_script;
            case 2: return voter_t::drep_key;
            case 3: return voter_t::drep_script;
            case 4: return voter_t::pool_key;
            default: throw error(fmt::format("unsupported voter type: {}", typ));
        }
    }

    voter_t voter_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return { voter_type_from_cbor(it.read()), it.read().bytes() };
    }

    vote_t vote_from_cbor(cbor::zero2::value &v)
    {
        switch (const auto vote = v.uint(); vote) {
            case 0: return vote_t::no;
            case 1: return vote_t::yes;
            case 2: return vote_t::abstain;
            default: throw error(fmt::format("unsupported vote: {}", vote));
        }
    }

    voting_procedure_t voting_procedure_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return { vote_from_cbor(it.read()), decltype(anchor)::from_cbor(it.read()) };
    }
}

namespace turbo::cardano::conway {
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
