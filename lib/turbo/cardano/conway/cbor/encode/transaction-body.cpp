/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/allegra/block.hpp>
#include <turbo/cardano/babbage/cbor/encode/transaction-output.hpp>
#include <turbo/cardano/conway/cbor/decode/full/transaction-body.hpp>
#include <turbo/cardano/conway/transaction.hpp>
#include <turbo/plutus/types.hpp>

namespace turbo::cardano::conway {
    namespace {
        void native_script_to_cbor(era_encoder &enc, const buffer raw)
        {
            allegra::native_script_t::from_cbor(raw).to_cbor(enc);
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

        void body_to_cbor(
            era_encoder &enc, plutus::allocator &alloc,
            const full::transaction_body_t &full_body)
        {
            const auto &body = full_body.value;
            if (full_body.network_id && *full_body.network_id > 1) [[unlikely]]
                throw error("Conway network_id must be 0 or 1");
            if (body.donation && !*body.donation) [[unlikely]]
                throw error("Conway treasury donation must be positive");

            size_t count = 3;
            count += body.validity_end.has_value();
            count += !body.certs.empty();
            count += !body.withdrawals.empty();
            count += full_body.auxiliary_data_hash.has_value();
            count += body.validity_start.has_value();
            count += !body.mints.empty();
            count += full_body.script_data_hash.has_value();
            count += !body.collateral_inputs.empty();
            count += !body.required_signers.empty();
            count += full_body.network_id.has_value();
            count += body.collateral_return.has_value();
            count += body.collateral_value.has_value();
            count += !body.ref_inputs.empty();
            count += !body.votes.empty();
            count += !body.proposals.empty();
            count += body.current_treasury.has_value();
            count += body.donation.has_value();

            enc.map_compact(count, [&] {
                enc.uint(0);
                inputs_to_cbor(enc, body.inputs);
                if (!body.collateral_inputs.empty()) {
                    enc.uint(13);
                    inputs_to_cbor(enc, body.collateral_inputs);
                }
                if (!body.ref_inputs.empty()) {
                    enc.uint(18);
                    inputs_to_cbor(enc, body.ref_inputs);
                }
                enc.uint(1);
                outputs_to_cbor(enc, alloc, body.outputs);
                if (body.collateral_return) {
                    enc.uint(16);
                    output_to_cbor(enc, alloc, *body.collateral_return);
                }
                if (body.collateral_value)
                    enc.uint(17).uint(*body.collateral_value);
                enc.uint(2).uint(body.fee);
                if (body.validity_end)
                    enc.uint(3).uint(*body.validity_end);
                if (!body.certs.empty()) {
                    enc.uint(4).tag(258);
                    enc.array_compact(body.certs.size(), [&] {
                        for (const auto &certificate: body.certs)
                            certificate_t { certificate }.to_cbor(enc);
                    });
                }
                if (!body.withdrawals.empty()) {
                    enc.uint(5);
                    withdrawals_to_cbor(enc, body.withdrawals);
                }
                if (body.validity_start)
                    enc.uint(8).uint(*body.validity_start);
                if (!body.required_signers.empty()) {
                    enc.uint(14).tag(258);
                    enc.array_compact(body.required_signers.size(), [&] {
                        for (const auto &signer: body.required_signers)
                            enc.bytes(signer);
                    });
                }
                if (!body.mints.empty()) {
                    enc.uint(9);
                    mint_to_cbor(enc, body.mints);
                }
                if (full_body.script_data_hash)
                    enc.uint(11).bytes(*full_body.script_data_hash);
                if (full_body.auxiliary_data_hash)
                    enc.uint(7).bytes(*full_body.auxiliary_data_hash);
                if (full_body.network_id)
                    enc.uint(15).uint(*full_body.network_id);
                if (!body.votes.empty()) {
                    enc.uint(19);
                    body.votes.to_cbor(enc);
                }
                if (!body.proposals.empty()) {
                    enc.uint(20).tag(258);
                    enc.array_compact(body.proposals.size(), [&] {
                        for (const auto &proposal: body.proposals)
                            proposal.procedure.to_cbor(enc);
                    });
                }
                if (body.current_treasury)
                    enc.uint(21).uint(*body.current_treasury);
                if (body.donation)
                    enc.uint(22).uint(*body.donation);
            });
        }
    }

    void voting_procedures_t::to_cbor(era_encoder &enc) const
    {
        size_t voter_count = 0;
        const voter_t *previous = nullptr;
        for (const auto &item: *this) {
            if (!previous || (*previous <=> item.voter) != std::strong_ordering::equal) {
                ++voter_count;
                previous = &item.voter;
            }
        }
        enc.map_compact(voter_count, [&] {
            for (auto it = begin(); it != end();) {
                const auto voter = it->voter;
                auto voter_end = it;
                while (voter_end != end()
                        && (voter_end->voter <=> voter) == std::strong_ordering::equal)
                    ++voter_end;
                enc.array(2).uint(static_cast<uint8_t>(voter.type)).bytes(voter.hash);
                enc.map_compact(std::distance(it, voter_end), [&] {
                    for (; it != voter_end; ++it) {
                        it->action_id.to_cbor(enc);
                        it->voting_procedure.to_cbor(enc);
                    }
                });
            }
        });
    }

    void proposal_procedures_t::to_cbor(era_encoder &enc) const
    {
        enc.tag(258);
        enc.array_compact(size(), [&] {
            for (const auto &proposal: *this)
                proposal.to_cbor(enc);
        });
    }

    void full::transaction_body_t::to_cbor(era_encoder &enc) const
    {
        plutus::allocator alloc {};
        body_to_cbor(enc, alloc, *this);
    }

    void transaction_body_t::to_cbor(era_encoder &enc) const
    {
        full::transaction_body_t { *this }.to_cbor(enc);
    }
}
