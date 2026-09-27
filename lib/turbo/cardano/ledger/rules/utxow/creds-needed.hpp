#pragma once
/* Copyright (c) 2026 R2 Rationality OÜ. License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */
#include <algorithm>
#include <turbo/cardano/conway/block.hpp>

namespace turbo::cardano::ledger::rules {
    inline credential_t voter_credential(const voter_t &voter)
    {
        switch (voter.type) {
            case voter_t::const_comm_script:
            case voter_t::drep_script:
                return { voter.hash, true };
            case voter_t::const_comm_key:
            case voter_t::drep_key:
            case voter_t::pool_key:
                return { voter.hash, false };
            default:
                throw error("unsupported governance voter type");
        }
    }

    inline std::optional<script_hash> proposal_policy(const gov_action_t &action)
    {
        return std::visit([](const auto &a) -> std::optional<script_hash> {
            using T = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<T, gov_action_t::parameter_change_t>
                    || std::is_same_v<T, gov_action_t::treasury_withdrawals_t>)
                return a.policy_id;
            return {};
        }, action.val);
    }

    // Projections of Script.Validation.credsNeeded and rdptr. Hashes identify
    // scripts, while pointers identify invocations; neither domain replaces the other.
    struct credential_requirements {
        flat_set<key_hash> keys {};
        flat_set<script_hash> scripts {};
        flat_map<redeemer_id, script_hash> purposes {};

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.keys, self.scripts, self.purposes);
        }

        void observe(const credential_t &cred, std::optional<redeemer_id> pointer={})
        {
            if (cred.script) {
                scripts.emplace(cred.hash);
                if (pointer)
                    purposes.emplace(*pointer, cred.hash);
            } else {
                keys.emplace(cred.hash);
            }
        }

        void observe_payment(const address &addr, std::optional<redeemer_id> pointer={})
        {
            // pay_id projects a Byron address to its root, in the same key-hash
            // domain as Shelley payment keys (bootstrapKeyHash in cardano-ledger).
            const auto pay = addr.pay_id();
            observe({ pay.hash, pay.type == pay_ident::ident_type::SHELLEY_SCRIPT }, pointer);
        }

        void collect(const conway::tx &tx)
        {
            size_t index = 0;
            std::vector<const cert_t *> script_certificates {};
            tx.foreach_cert([&](const auto &cert) {
                if (const auto cred = cert.signing_cred(); cred) {
                    // rdptr(Cert c) selects the first occurrence of an equal certificate.
                    if (!cred->script || std::none_of(script_certificates.begin(), script_certificates.end(),
                            [&](const auto *previous) { return *previous == cert; })) {
                        observe(*cred, redeemer_id { redeemer_tag::cert, numeric_cast<uint32_t>(index) });
                        if (cred->script)
                            script_certificates.push_back(&cert);
                    }
                }
                ++index;
            });
            index = 0;
            tx.foreach_withdrawal([&](const tx_withdrawal &withdrawal) {
                if (withdrawal.address.network() != tx.block().config().shelley_network_id)
                    throw error("withdrawal network does not match the ledger");
                observe(withdrawal.address.stake_id(),
                    redeemer_id { redeemer_tag::reward, numeric_cast<uint32_t>(index++) });
            });
            index = 0;
            tx.foreach_mint([&](const auto &policy, const auto &) {
                observe({ policy, true }, redeemer_id { redeemer_tag::mint, numeric_cast<uint32_t>(index++) });
            });
            const voter_t *previous_voter = nullptr;
            index = 0;
            for (const auto &vote: tx.votes()) {
                const auto &voter = vote.voter;
                if (previous_voter && (voter <=> *previous_voter) == std::strong_ordering::equal)
                    continue;
                previous_voter = &voter;
                observe(voter_credential(voter), redeemer_id { redeemer_tag::vote, numeric_cast<uint32_t>(index++) });
            }
            index = 0;
            for (const auto &proposal: tx.proposals()) {
                if (const auto policy = proposal_policy(proposal.procedure.action))
                    observe({ *policy, true }, redeemer_id { redeemer_tag::propose, numeric_cast<uint32_t>(index) });
                ++index;
            }
            tx.foreach_required_signer([&](const auto &hash) { keys.emplace(hash); });
        }
    };
}
