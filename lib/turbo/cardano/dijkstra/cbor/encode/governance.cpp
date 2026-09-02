/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/transaction.hpp>

namespace turbo::cardano::dijkstra {
    namespace {
        void proposal_to_cbor(era_encoder &enc, const proposal_procedure_t &proposal)
        {
            enc.array(4).uint(proposal.deposit);
            byte_array<29> return_address {};
            return_address[0] = (proposal.return_addr.script ? 0xF0 : 0xE0)
                | (proposal.return_addr_network_id & 0xF);
            memcpy(return_address.data() + 1, proposal.return_addr.hash.data(), proposal.return_addr.hash.size());
            enc.bytes(return_address);
            governance_action_t { proposal.action }.to_cbor(enc);
            proposal.anchor.to_cbor(enc);
        }
    }

    void governance_action_t::to_cbor(era_encoder &enc) const
    {
        std::visit([&](const auto &action) {
            using T = std::decay_t<decltype(action)>;
            if constexpr (std::is_same_v<T, gov_action_t::parameter_change_t>) {
                enc.array(4).uint(0);
                action.prev_action_id.to_cbor(enc);
                protocol_param_update_t { action.update }.to_cbor(enc);
                action.policy_id.to_cbor(enc);
            } else {
                action.to_cbor(enc);
            }
        }, value.val);
    }

    void proposal_procedures_t::to_cbor(era_encoder &enc) const
    {
        if (empty()) [[unlikely]]
            throw error("Dijkstra proposal procedures must be nonempty");
        enc.tag(258);
        enc.array_compact(size(), [&] {
            for (const auto &proposal: *this)
                proposal_to_cbor(enc, proposal);
        });
    }
}
