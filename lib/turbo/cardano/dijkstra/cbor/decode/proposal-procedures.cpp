/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/transaction.hpp>

namespace turbo::cardano::dijkstra {
    namespace {
        proposal_procedure_t proposal_from_cbor(cbor::zero2::value &v)
        {
            auto &it = v.array();
            const auto deposit = it.read().uint();
            const reward_id_t return_address { it.read().bytes() };
            proposal_procedure_t res {
                deposit,
                static_cast<stake_ident>(return_address),
                return_address.network_id(),
                governance_action_t::from_cbor(it.read()).value,
                anchor_t::from_cbor(it.read())
            };
            if (!it.done()) [[unlikely]]
                throw error("unexpected trailing Dijkstra proposal procedure elements");
            return res;
        }
    }

    proposal_procedures_t proposal_procedures_t::from_cbor(cbor::zero2::value &v)
    {
        proposal_procedures_t res {};
        auto *items = &v;
        if (v.type() == cbor::major_type::tag) {
            auto &tag = v.tag();
            if (tag.id() != 258) [[unlikely]]
                throw error(fmt::format("expected proposal set tag 258 but got: {}", tag.id()));
            items = &tag.read();
        }
        if (!items->indefinite()) [[likely]]
            res.reserve(items->special_uint());
        auto &it = items->array();
        while (!it.done()) {
            auto &item = it.read();
            auto proposal = proposal_from_cbor(item);
            for (const auto &existing: res) {
                if (existing == proposal) [[unlikely]]
                    throw error("duplicate Dijkstra proposal procedure");
            }
            res.emplace_back(std::move(proposal));
        }
        if (res.empty()) [[unlikely]]
            throw error("Dijkstra proposal procedures must be nonempty when supplied");
        return res;
    }
}
