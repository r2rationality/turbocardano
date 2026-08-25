/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/conway/transaction.hpp>

namespace turbo::cardano {
    proposal_procedure_t proposal_procedure_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        const auto deposit = it.read().uint();
        const reward_id_t return_addr { it.read().bytes() };
        return {
            deposit,
            static_cast<stake_ident>(return_addr),
            return_addr.network_id(),
            gov_action_t::from_cbor(it.read()),
            anchor_t::from_cbor(it.read())
        };
    }

}

namespace turbo::cardano::conway {
    proposal_procedures_t proposal_procedures_t::from_cbor(cbor::zero2::value &v)
    {
        proposal_procedures_t res {};
        auto *items = &v;
        if (v.type() == cbor::major_type::tag) {
            auto &reader = v.tag();
            if (reader.id() != 258) [[unlikely]]
                throw error(fmt::format("expected a tag with id 258 but got: {}!", reader.id()));
            items = &reader.read();
        }
        if (!items->indefinite()) [[likely]]
            res.reserve(items->special_uint());
        auto &it = items->array();
        while (!it.done())
            res.emplace_back(proposal_procedure_t::from_cbor(it.read()));
        return res;
    }
}
