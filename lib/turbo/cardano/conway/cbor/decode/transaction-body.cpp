/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/conway/transaction.hpp>

namespace turbo::cardano::conway {
    using namespace plutus;

    transaction_body_t transaction_body_t::from_cbor(cbor::zero2::value &v)
    {
        transaction_body_t res {};
        std::optional<proposal_procedures_t> proposals {};
        uint32_t seen = 0;
        auto &it = v.map();
        while (!it.done()) {
            auto &mk = it.read_key();
            const auto typ = mk.uint();
            if (typ > 22) [[unlikely]]
                throw error(fmt::format("unsupported Conway transaction body key: {}", typ));
            const auto mask = uint32_t { 1 } << typ;
            if (seen & mask) [[unlikely]]
                throw error(fmt::format("duplicate Conway transaction body key: {}", typ));
            seen |= mask;
            auto &mv = it.read_val(std::move(mk));
            switch (typ) {
                case 0: res.inputs = transaction_inputs_t::from_cbor(mv); break;
                case 1: {
                    auto outputs = transaction_outputs_t::from_cbor(mv);
                    static_cast<tx_output_list &>(res.outputs).swap(outputs.value);
                    break;
                }
                case 2: res.fee = mv.uint(); break;
                case 3: res.validity_end.emplace(mv.uint()); break;
                case 4:
                    res.certs = certificates_t::from_cbor(mv);
                    if (res.certs.empty()) [[unlikely]]
                        throw error("Conway certificates must be nonempty when supplied");
                    break;
                case 5:
                    res.withdrawals = shelley::withdrawals_t::from_cbor(mv);
                    if (res.withdrawals.empty()) [[unlikely]]
                        throw error("Conway withdrawals must be nonempty when supplied");
                    break;
                case 7: static_cast<void>(hash_32 { mv.bytes() }); break;
                case 8: res.validity_start.emplace(mv.uint()); break;
                case 9:
                    res.mints = mint_t::from_cbor(mv);
                    if (res.mints.empty()) [[unlikely]]
                        throw error("Conway mint must be nonempty when supplied");
                    break;
                case 11: static_cast<void>(hash_32 { mv.bytes() }); break;
                case 13:
                    res.collateral_inputs = transaction_inputs_t::from_cbor(mv);
                    if (res.collateral_inputs.empty()) [[unlikely]]
                        throw error("Conway collateral inputs must be nonempty when supplied");
                    break;
                case 14:
                    res.required_signers = required_signers_t::from_cbor(mv);
                    if (res.required_signers.empty()) [[unlikely]]
                        throw error("Conway required signers must be nonempty when supplied");
                    break;
                case 15: {
                    const auto network_id = mv.uint();
                    if (network_id > 1) [[unlikely]]
                        throw error("network_id must be 0 or 1");
                    break;
                }
                case 16: res.collateral_return.emplace(std::move(transaction_output_t::from_cbor(mv).value)); break;
                case 17: res.collateral_value.emplace(mv.uint()); break;
                case 18:
                    res.ref_inputs = transaction_inputs_t::from_cbor(mv);
                    if (res.ref_inputs.empty()) [[unlikely]]
                        throw error("Conway reference inputs must be nonempty when supplied");
                    break;
                case 19:
                    res.votes = voting_procedures_t::from_cbor(mv);
                    if (res.votes.empty()) [[unlikely]]
                        throw error("Conway voting procedures must be nonempty when supplied");
                    break;
                case 20:
                    proposals.emplace(proposal_procedures_t::from_cbor(mv));
                    if (proposals->empty()) [[unlikely]]
                        throw error("Conway proposal procedures must be nonempty when supplied");
                    break;
                case 21: res.current_treasury = mv.uint(); break;
                case 22: res.donation = static_cast<uint64_t>(positive_coin_t { mv.uint() }); break;
                [[unlikely]] default: throw error(fmt::format("unsupported Conway transaction body key: {}", typ));
            }
        }
        constexpr uint32_t required =
            (uint32_t { 1 } << 0) | (uint32_t { 1 } << 1) | (uint32_t { 1 } << 2);
        if ((seen & required) != required) [[unlikely]]
            throw error("Conway transaction body is missing a required field");
        res.raw = v.data_raw();
        if (proposals) {
            res.proposals.reserve(proposals->size());
            const auto tx_id = crypto::blake2b::digest<tx_hash>(res.raw);
            res.hash = tx_id;
            size_t prop_idx = 0;
            for (auto &&p: *proposals)
                res.proposals.emplace_hint(res.proposals.end(), gov_action_id_t { tx_id, numeric_cast<uint16_t>(prop_idx++) }, std::move(p));
        }
        return res;
    }

}
