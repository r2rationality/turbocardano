/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/babbage/cbor/encode/transaction-output.hpp>
#include <turbo/cardano/dijkstra/transaction.hpp>
#include <turbo/plutus/types.hpp>

namespace turbo::cardano::dijkstra {
    namespace {
        void native_script_to_cbor(era_encoder &enc, const buffer raw)
        {
            native_script_t::from_cbor(raw).to_cbor(enc);
        }

        void output_to_cbor(
            era_encoder &enc, plutus::allocator &alloc, const tx_out_data &output)
        {
            babbage::detail::transaction_output_to_cbor_semantic(
                enc, output, alloc, native_script_to_cbor);
        }

        void inputs_to_cbor(era_encoder &enc, const input_set &inputs)
        {
            enc.tag(258);
            enc.array_compact(inputs.size(), [&] {
                for (const auto &input: inputs)
                    enc.array(2).bytes(input.hash).uint(static_cast<size_t>(input.idx));
            });
        }

        void outputs_to_cbor(
            era_encoder &enc, plutus::allocator &alloc, const tx_output_list &outputs)
        {
            enc.array_compact(outputs.size(), [&] {
                for (const auto &output: outputs)
                    output_to_cbor(enc, alloc, output);
            });
        }

        void withdrawals_to_cbor(era_encoder &enc, const withdrawal_map &withdrawals)
        {
            enc.map_compact(withdrawals.size(), [&] {
                for (const auto &[account, amount]: withdrawals)
                    enc.bytes(account).uint(amount);
            });
        }

        void mint_to_cbor(era_encoder &enc, const multi_mint_map &mint)
        {
            enc.map_compact(mint.size(), [&] {
                for (const auto &[policy, assets]: mint) {
                    enc.bytes(policy);
                    enc.map_compact(assets.size(), [&] {
                        for (const auto &[name, amount]: assets) {
                            name.to_cbor(enc);
                            if (amount >= 0)
                                enc.uint(numeric_cast<uint64_t>(amount));
                            else
                                enc.nint(numeric_cast<uint64_t>(-(amount + 1)));
                        }
                    });
                }
            });
        }

        void voting_procedure_to_cbor(era_encoder &enc, const voting_procedure_t &procedure)
        {
            enc.array(2).uint(static_cast<uint8_t>(procedure.vote));
            procedure.anchor.to_cbor(enc);
        }

        void votes_to_cbor(era_encoder &enc, const conway::vote_set &votes)
        {
            size_t voter_count = 0;
            const voter_t *previous = nullptr;
            for (const auto &vote: votes) {
                if (!previous || (*previous <=> vote.voter) != std::strong_ordering::equal) {
                    ++voter_count;
                    previous = &vote.voter;
                }
            }
            enc.map_compact(voter_count, [&] {
                for (auto it = votes.begin(); it != votes.end();) {
                    const auto voter = it->voter;
                    auto end = it;
                    while (end != votes.end() && (end->voter <=> voter) == std::strong_ordering::equal)
                        ++end;
                    enc.array(2).uint(static_cast<uint8_t>(voter.type)).bytes(voter.hash);
                    enc.map_compact(std::distance(it, end), [&] {
                        for (; it != end; ++it) {
                            it->action_id.to_cbor(enc);
                            voting_procedure_to_cbor(enc, it->voting_procedure);
                        }
                    });
                }
            });
        }

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

        void proposals_to_cbor(era_encoder &enc, const conway::proposal_set &proposals)
        {
            enc.tag(258);
            enc.array_compact(proposals.size(), [&] {
                for (const auto &proposal: proposals)
                    proposal_to_cbor(enc, proposal.procedure);
            });
        }

        void required_guards_to_cbor(
            era_encoder &enc, plutus::allocator &alloc,
            const required_top_level_guards_t &guards)
        {
            enc.map_compact(guards.size(), [&] {
                for (const auto &[credential, data]: guards) {
                    credential.to_cbor(enc);
                    if (data)
                        data->to_cbor(enc, alloc);
                    else
                        enc.s_null();
                }
            });
        }

        void balance_intervals_to_cbor(era_encoder &enc, const account_balance_intervals_t &intervals)
        {
            enc.map_compact(intervals.size(), [&] {
                for (const auto &[credential, interval]: intervals) {
                    credential.to_cbor(enc);
                    interval.to_cbor(enc);
                }
            });
        }

        template<typename BODY>
        void body_to_cbor(
            era_encoder &enc, plutus::allocator &alloc,
            const BODY &body)
        {
            constexpr bool top_level = std::is_same_v<BODY, transaction_body_t>;
            if (body.network_id && *body.network_id > 1) [[unlikely]]
                throw error("Dijkstra network_id must be 0 or 1");
            if (body.donation && !*body.donation) [[unlikely]]
                throw error("Dijkstra treasury donation must be positive");
            size_t count = 2 + top_level;
            count += body.validity_end.has_value();
            count += !body.certs.empty();
            count += !body.withdrawals.empty();
            count += body.auxiliary_data_hash.has_value();
            count += body.validity_start.has_value();
            count += !body.mints.empty();
            count += body.script_data_hash.has_value();
            count += top_level && !body.collateral_inputs.empty();
            count += !body.guards.empty();
            count += body.network_id.has_value();
            count += top_level && body.collateral_return.has_value();
            count += top_level && body.collateral_value.has_value();
            count += !body.ref_inputs.empty();
            count += !body.votes.empty();
            count += !body.proposals.empty();
            count += body.current_treasury.has_value();
            count += body.donation.has_value();
            if constexpr (top_level)
                count += !body.sub_transactions.empty();
            count += !body.required_top_level_guards.empty();
            count += !body.direct_deposits.empty();
            count += !body.account_balance_intervals.empty();
            if constexpr (top_level)
                count += !body.starting_account_balance_intervals.empty();

            enc.map_compact(count, [&] {
                enc.uint(0);
                inputs_to_cbor(enc, body.inputs);
                if constexpr (top_level) {
                    if (!body.collateral_inputs.empty()) {
                        enc.uint(13);
                        inputs_to_cbor(enc, body.collateral_inputs);
                    }
                }
                if (!body.ref_inputs.empty()) {
                    enc.uint(18);
                    inputs_to_cbor(enc, body.ref_inputs);
                }
                enc.uint(1);
                outputs_to_cbor(enc, alloc, body.outputs);
                if constexpr (top_level) {
                    if (body.collateral_return) {
                        enc.uint(16);
                        output_to_cbor(enc, alloc, *body.collateral_return);
                    }
                    if (body.collateral_value)
                        enc.uint(17).uint(*body.collateral_value);
                    enc.uint(2).uint(body.fee);
                }
                if (body.validity_end)
                    enc.uint(3).uint(*body.validity_end);
                if (!body.certs.empty()) {
                    enc.uint(4).tag(258);
                    enc.array_compact(body.certs.size(), [&] {
                        for (const auto &cert: body.certs)
                            certificate_t { cert }.to_cbor(enc);
                    });
                }
                if (!body.withdrawals.empty()) {
                    enc.uint(5);
                    withdrawals_to_cbor(enc, body.withdrawals);
                }
                if (body.validity_start)
                    enc.uint(8).uint(*body.validity_start);
                if (!body.guards.empty()) {
                    enc.uint(14);
                    body.guards.to_cbor(enc);
                }
                if (!body.mints.empty()) {
                    enc.uint(9);
                    mint_to_cbor(enc, body.mints);
                }
                if (body.script_data_hash)
                    enc.uint(11).bytes(*body.script_data_hash);
                if (body.auxiliary_data_hash)
                    enc.uint(7).bytes(*body.auxiliary_data_hash);
                if (body.network_id)
                    enc.uint(15).uint(*body.network_id);
                if (!body.votes.empty()) {
                    enc.uint(19);
                    votes_to_cbor(enc, body.votes);
                }
                if (!body.proposals.empty()) {
                    enc.uint(20);
                    proposals_to_cbor(enc, body.proposals);
                }
                if (body.current_treasury)
                    enc.uint(21).uint(*body.current_treasury);
                if (body.donation)
                    enc.uint(22).uint(*body.donation);
                if constexpr (top_level) {
                    if (!body.sub_transactions.empty()) {
                        enc.uint(23).tag(258);
                        enc.array_compact(body.sub_transactions.size(), [&] {
                            for (const auto &transaction: body.sub_transactions)
                                transaction.to_cbor(enc);
                        });
                    }
                }
                if (!body.required_top_level_guards.empty()) {
                    enc.uint(24);
                    required_guards_to_cbor(enc, alloc, body.required_top_level_guards);
                }
                if (!body.direct_deposits.empty()) {
                    enc.uint(25);
                    withdrawals_to_cbor(enc, body.direct_deposits);
                }
                if (!body.account_balance_intervals.empty()) {
                    enc.uint(26);
                    balance_intervals_to_cbor(enc, body.account_balance_intervals);
                }
                if constexpr (top_level) {
                    if (!body.starting_account_balance_intervals.empty()) {
                        enc.uint(27);
                        balance_intervals_to_cbor(enc, body.starting_account_balance_intervals);
                    }
                }
            });
        }
    }

    void transaction_body_t::to_cbor(era_encoder &enc) const
    {
        plutus::allocator alloc {};
        body_to_cbor(enc, alloc, *this);
    }

    void sub_transaction_body_t::to_cbor(era_encoder &enc) const
    {
        plutus::allocator alloc {};
        body_to_cbor(enc, alloc, *this);
    }
}
